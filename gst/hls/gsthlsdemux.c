/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-hlsdemux
 *
 * HTTP Live Streaming demuxer element.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch souphttpsrc location=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8 ! hlsdemux ! decodebin2 ! ffmpegcolorspace ! videoscale ! autovideosink
 * ]|
 * </refsect2>
 *
 * Last reviewed on 2010-10-07
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif


#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include "gsthlsdemux.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-hls"));

static GstStaticPadTemplate fetchertemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_hls_demux_debug);
#define GST_CAT_DEFAULT gst_hls_demux_debug

enum
{
  PROP_0,

  PROP_FRAGMENTS_CACHE,
  PROP_BITRATE_SWITCH_TOLERANCE,
  PROP_LAST
};

static const float update_interval_factor[] = { 1, 0.5, 1.5, 3 };

#define DEFAULT_FRAGMENTS_CACHE 3
#define DEFAULT_FAILED_COUNT 3
#define DEFAULT_BITRATE_SWITCH_TOLERANCE 0.4

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
static GstBusSyncReply gst_hls_demux_fetcher_bus_handler (GstBus * bus,
    GstMessage * message, gpointer data);
static GstFlowReturn gst_hls_demux_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_hls_demux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_hls_demux_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_hls_demux_src_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_hls_demux_fetcher_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_hls_demux_fetcher_sink_event (GstPad * pad,
    GstEvent * event);
static void gst_hls_demux_loop (GstHLSDemux * demux);
static void gst_hls_demux_stop (GstHLSDemux * demux);
static void gst_hls_demux_stop_fetcher_locked (GstHLSDemux * demux,
    gboolean cancelled);
static void gst_hls_demux_stop_update (GstHLSDemux * demux);
static gboolean gst_hls_demux_start_update (GstHLSDemux * demux);
static gboolean gst_hls_demux_cache_fragments (GstHLSDemux * demux);
static gboolean gst_hls_demux_schedule (GstHLSDemux * demux);
static gboolean gst_hls_demux_switch_playlist (GstHLSDemux * demux);
static gboolean gst_hls_demux_get_next_fragment (GstHLSDemux * demux);
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux);
static void gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose);
static gboolean gst_hls_demux_set_location (GstHLSDemux * demux,
    const gchar * uri);
static gchar *gst_hls_src_buf_to_utf8_playlist (gchar * string, guint size);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (gst_hls_demux_debug, "hlsdemux", 0,
      "hlsdemux element");
}

GST_BOILERPLATE_FULL (GstHLSDemux, gst_hls_demux, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_hls_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class,
      "HLS Demuxer",
      "Demuxer/URIList",
      "HTTP Live Streaming demuxer",
      "Marc-Andre Lureau <marcandre.lureau@gmail.com>\n"
      "Andoni Morales Alastruey <ylatuya@gmail.com>");
}

static void
gst_hls_demux_dispose (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  g_cond_free (demux->fetcher_cond);
  g_mutex_free (demux->fetcher_lock);

  g_cond_free (demux->thread_cond);
  g_mutex_free (demux->thread_lock);

  gst_task_join (demux->task);
  gst_object_unref (demux->task);
  g_static_rec_mutex_free (&demux->task_lock);

  gst_object_unref (demux->fetcher_bus);
  gst_object_unref (demux->fetcherpad);

  gst_hls_demux_reset (demux, TRUE);

  g_queue_free (demux->queue);
  gst_object_unref (demux->download);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_hls_demux_class_init (GstHLSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_hls_demux_set_property;
  gobject_class->get_property = gst_hls_demux_get_property;
  gobject_class->dispose = gst_hls_demux_dispose;

  g_object_class_install_property (gobject_class, PROP_FRAGMENTS_CACHE,
      g_param_spec_uint ("fragments-cache", "Fragments cache",
          "Number of fragments needed to be cached to start playing",
          2, G_MAXUINT, DEFAULT_FRAGMENTS_CACHE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE_SWITCH_TOLERANCE,
      g_param_spec_float ("bitrate-switch-tolerance",
          "Bitrate switch tolerance",
          "Tolerance with respect of the fragment duration to switch to "
          "a different bitrate if the client is too slow/fast.",
          0, 1, DEFAULT_BITRATE_SWITCH_TOLERANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);
}

static void
gst_hls_demux_init (GstHLSDemux * demux, GstHLSDemuxClass * klass)
{
  /* sink pad */
  demux->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* fetcher pad */
  demux->fetcherpad =
      gst_pad_new_from_static_template (&fetchertemplate, "sink");
  gst_pad_set_chain_function (demux->fetcherpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_fetcher_chain));
  gst_pad_set_event_function (demux->fetcherpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_fetcher_sink_event));
  gst_pad_set_element_private (demux->fetcherpad, demux);
  gst_pad_activate_push (demux->fetcherpad, TRUE);

  demux->do_typefind = TRUE;

  /* Properties */
  demux->fragments_cache = DEFAULT_FRAGMENTS_CACHE;
  demux->bitrate_switch_tol = DEFAULT_BITRATE_SWITCH_TOLERANCE;

  demux->download = gst_adapter_new ();
  demux->fetcher_bus = gst_bus_new ();
  gst_bus_set_sync_handler (demux->fetcher_bus,
      gst_hls_demux_fetcher_bus_handler, demux);
  demux->thread_cond = g_cond_new ();
  demux->thread_lock = g_mutex_new ();
  demux->fetcher_cond = g_cond_new ();
  demux->fetcher_lock = g_mutex_new ();
  demux->queue = g_queue_new ();
  g_static_rec_mutex_init (&demux->task_lock);
  /* FIXME: This really should be a pad task instead */
  demux->task = gst_task_create ((GstTaskFunction) gst_hls_demux_loop, demux);
  gst_task_set_lock (demux->task, &demux->task_lock);
}

static void
gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      demux->fragments_cache = g_value_get_uint (value);
      break;
    case PROP_BITRATE_SWITCH_TOLERANCE:
      demux->bitrate_switch_tol = g_value_get_float (value);
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
      g_value_set_uint (value, demux->fragments_cache);
      break;
    case PROP_BITRATE_SWITCH_TOLERANCE:
      g_value_set_float (value, demux->bitrate_switch_tol);
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
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* Start the streaming loop in paused only if we already received
         the main playlist. It might have been stopped if we were in PAUSED
         state and we filled our queue with enough cached fragments
       */
      if (gst_m3u8_client_get_uri (demux->client)[0] != '\0')
        gst_hls_demux_start_update (demux);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_hls_demux_stop_update (demux);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      demux->cancelled = TRUE;
      gst_hls_demux_stop (demux);
      gst_task_join (demux->task);
      gst_hls_demux_reset (demux, FALSE);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_hls_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstHLSDemux *demux;

  demux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

  switch (event->type) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GList *walk;
      gint current_pos;
      gint current_sequence;
      gint target_second;
      GstM3U8MediaFile *file;

      GST_INFO_OBJECT (demux, "Received GST_EVENT_SEEK");

      if (gst_m3u8_client_is_live (demux->client)) {
        GST_WARNING_OBJECT (demux, "Received seek event for live stream");
        return FALSE;
      }

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      GST_DEBUG_OBJECT (demux, "seek event, rate: %f start: %" GST_TIME_FORMAT
          " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      GST_M3U8_CLIENT_LOCK (demux->client);
      file = GST_M3U8_MEDIA_FILE (demux->client->current->files->data);
      current_sequence = file->sequence;
      current_pos = 0;
      target_second = start / GST_SECOND;
      GST_DEBUG_OBJECT (demux, "Target seek to %d", target_second);
      for (walk = demux->client->current->files; walk; walk = walk->next) {
        file = walk->data;

        current_sequence = file->sequence;
        if (current_pos <= target_second
            && target_second < current_pos + file->duration) {
          break;
        }
        current_pos += file->duration;
      }
      GST_M3U8_CLIENT_UNLOCK (demux->client);

      if (walk == NULL) {
        GST_WARNING_OBJECT (demux, "Could not find seeked fragment");
        return FALSE;
      }

      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush start");
        gst_pad_push_event (demux->srcpad, gst_event_new_flush_start ());
      }

      demux->cancelled = TRUE;
      gst_task_pause (demux->task);
      g_mutex_lock (demux->fetcher_lock);
      gst_hls_demux_stop_fetcher_locked (demux, TRUE);
      g_mutex_unlock (demux->fetcher_lock);
      gst_hls_demux_stop_update (demux);
      gst_task_pause (demux->task);

      /* wait for streaming to finish */
      g_static_rec_mutex_lock (&demux->task_lock);

      demux->need_cache = TRUE;
      while (!g_queue_is_empty (demux->queue)) {
        GstBuffer *buf = g_queue_pop_head (demux->queue);
        gst_buffer_unref (buf);
      }
      g_queue_clear (demux->queue);
      gst_adapter_clear (demux->download);

      GST_M3U8_CLIENT_LOCK (demux->client);
      GST_DEBUG_OBJECT (demux, "seeking to sequence %d", current_sequence);
      demux->client->sequence = current_sequence;
      gst_m3u8_client_get_current_position (demux->client, &demux->position);
      demux->position_shift = start - demux->position;
      demux->need_segment = TRUE;
      GST_M3U8_CLIENT_UNLOCK (demux->client);


      if (flags & GST_SEEK_FLAG_FLUSH) {
        GST_DEBUG_OBJECT (demux, "sending flush stop");
        gst_pad_push_event (demux->srcpad, gst_event_new_flush_stop ());
      }

      demux->cancelled = FALSE;
      gst_task_start (demux->task);
      g_static_rec_mutex_unlock (&demux->task_lock);

      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static gboolean
gst_hls_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_parent (pad));
  GstQuery *query;
  gboolean ret;
  gchar *uri;


  switch (event->type) {
    case GST_EVENT_EOS:{
      gchar *playlist;

      if (demux->playlist == NULL) {
        GST_WARNING_OBJECT (demux, "Received EOS without a playlist.");
        break;
      }

      GST_DEBUG_OBJECT (demux,
          "Got EOS on the sink pad: main playlist fetched");

      query = gst_query_new_uri ();
      ret = gst_pad_peer_query (demux->sinkpad, query);
      if (ret) {
        gst_query_parse_uri (query, &uri);
        gst_hls_demux_set_location (demux, uri);
        g_free (uri);
      }
      gst_query_unref (query);

      playlist = gst_hls_src_buf_to_utf8_playlist ((gchar *)
          GST_BUFFER_DATA (demux->playlist), GST_BUFFER_SIZE (demux->playlist));
      gst_buffer_unref (demux->playlist);
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

      gst_task_start (demux->task);
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
gst_hls_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstHLSDemux *hlsdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  hlsdemux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

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

static gboolean
gst_hls_demux_fetcher_sink_event (GstPad * pad, GstEvent * event)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

  switch (event->type) {
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (demux, "Got EOS on the fetcher pad");
      /* signal we have fetched the URI */
      if (!demux->cancelled) {
        g_cond_broadcast (demux->fetcher_cond);
      }
    }
    default:
      break;
  }

  gst_event_unref (event);
  return FALSE;
}

static GstFlowReturn
gst_hls_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_parent (pad));

  if (demux->playlist == NULL)
    demux->playlist = buf;
  else
    demux->playlist = gst_buffer_join (demux->playlist, buf);

  gst_object_unref (demux);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_fetcher_chain (GstPad * pad, GstBuffer * buf)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

  /* The source element can be an http source element. In case we get a 404,
   * the html response will be sent downstream and the adapter
   * will not be null, which might make us think that the request proceed
   * successfully. But it will also post an error message in the bus that
   * is handled synchronously and that will set demux->fetcher_error to TRUE,
   * which is used to discard this buffer with the html response. */
  if (demux->fetcher_error) {
    goto done;
  }

  gst_adapter_push (demux->download, buf);

done:
  {
    return GST_FLOW_OK;
  }
}

static void
gst_hls_demux_stop_fetcher_locked (GstHLSDemux * demux, gboolean cancelled)
{
  GstPad *pad;

  /* When the fetcher is stopped while it's downloading, we will get an EOS that
   * unblocks the fetcher thread and tries to stop it again from that thread.
   * Here we check if the fetcher as already been stopped before continuing */
  if (demux->fetcher == NULL || demux->stopping_fetcher)
    return;

  GST_DEBUG_OBJECT (demux, "Stopping fetcher.");
  demux->stopping_fetcher = TRUE;
  /* set the element state to NULL */
  gst_element_set_state (demux->fetcher, GST_STATE_NULL);
  gst_element_get_state (demux->fetcher, NULL, NULL, GST_CLOCK_TIME_NONE);
  /* unlink it from the internal pad */
  pad = gst_pad_get_peer (demux->fetcherpad);
  if (pad) {
    gst_pad_unlink (pad, demux->fetcherpad);
    gst_object_unref (pad);
  }
  /* and finally unref it */
  gst_object_unref (demux->fetcher);
  demux->fetcher = NULL;

  /* if we stopped it to cancell a download, free the cached buffer */
  if (cancelled && gst_adapter_available (demux->download)) {
    gst_adapter_clear (demux->download);
  }
  /* signal the fetcher thread that the download has finished/cancelled */
  if (cancelled)
    g_cond_broadcast (demux->fetcher_cond);
}

static void
gst_hls_demux_stop (GstHLSDemux * demux)
{
  g_mutex_lock (demux->fetcher_lock);
  gst_hls_demux_stop_fetcher_locked (demux, TRUE);
  g_mutex_unlock (demux->fetcher_lock);
  gst_task_stop (demux->task);
  gst_hls_demux_stop_update (demux);
}

static void
switch_pads (GstHLSDemux * demux, GstCaps * newcaps)
{
  GstPad *oldpad = demux->srcpad;

  GST_DEBUG ("Switching pads (oldpad:%p)", oldpad);

  /* FIXME: This is a workaround for a bug in playsink.
   * If we're switching from an audio-only or video-only fragment
   * to an audio-video segment, the new sink doesn't know about
   * the current running time and audio/video will go out of sync.
   *
   * This should be fixed in playsink by distributing the
   * current running time to newly created sinks and is
   * fixed in 0.11 with the new segments.
   */
  if (demux->srcpad)
    gst_pad_push_event (demux->srcpad, gst_event_new_flush_stop ());

  /* First create and activate new pad */
  demux->srcpad = gst_pad_new_from_static_template (&srctemplate, NULL);
  gst_pad_set_event_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_src_event));
  gst_pad_set_query_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_src_query));
  gst_pad_set_element_private (demux->srcpad, demux);
  gst_pad_set_active (demux->srcpad, TRUE);
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

static void
gst_hls_demux_loop (GstHLSDemux * demux)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  /* Loop for the source pad task. The task is started when we have
   * received the main playlist from the source element. It tries first to
   * cache the first fragments and then it waits until it has more data in the
   * queue. This task is woken up when we push a new fragment to the queue or
   * when we reached the end of the playlist  */

  if (G_UNLIKELY (demux->need_cache)) {
    if (!gst_hls_demux_cache_fragments (demux))
      goto cache_error;

    /* we can start now the updates thread (only if on playing) */
    if (GST_STATE (demux) == GST_STATE_PLAYING)
      gst_hls_demux_start_update (demux);
    GST_INFO_OBJECT (demux, "First fragments cached successfully");
  }

  if (g_queue_is_empty (demux->queue)) {
    if (demux->end_of_playlist)
      goto end_of_playlist;

    goto pause_task;
  }

  buf = g_queue_pop_head (demux->queue);

  /* Figure out if we need to create/switch pads */
  if (G_UNLIKELY (!demux->srcpad
          || GST_BUFFER_CAPS (buf) != GST_PAD_CAPS (demux->srcpad)
          || demux->need_segment)) {
    switch_pads (demux, GST_BUFFER_CAPS (buf));
    demux->need_segment = TRUE;
  }
  if (demux->need_segment) {
    GstClockTime start = demux->position + demux->position_shift;
    /* And send a newsegment */
    GST_DEBUG_OBJECT (demux, "Sending new-segment. segment start:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (start));
    gst_pad_push_event (demux->srcpad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
            start, GST_CLOCK_TIME_NONE, start));
    demux->need_segment = FALSE;
    demux->position_shift = 0;
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buf)))
    demux->position += GST_BUFFER_DURATION (buf);

  ret = gst_pad_push (demux->srcpad, buf);
  if (ret != GST_FLOW_OK)
    goto error;

  return;

end_of_playlist:
  {
    GST_DEBUG_OBJECT (demux, "Reached end of playlist, sending EOS");
    gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    gst_hls_demux_stop (demux);
    return;
  }

cache_error:
  {
    gst_task_pause (demux->task);
    if (!demux->cancelled) {
      GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
          ("Could not cache the first fragments"), (NULL));
      gst_hls_demux_stop (demux);
    }
    return;
  }

error:
  {
    /* FIXME: handle error */
    GST_DEBUG_OBJECT (demux, "error, stopping task");
    gst_hls_demux_stop (demux);
    return;
  }

pause_task:
  {
    gst_task_pause (demux->task);
    return;
  }
}

static GstBusSyncReply
gst_hls_demux_fetcher_bus_handler (GstBus * bus,
    GstMessage * message, gpointer data)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (data);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    demux->fetcher_error = TRUE;
    if (!demux->cancelled) {
      g_mutex_lock (demux->fetcher_lock);
      g_cond_broadcast (demux->fetcher_cond);
      g_mutex_unlock (demux->fetcher_lock);
    }
  }

  gst_message_unref (message);
  return GST_BUS_DROP;
}

static gboolean
gst_hls_demux_make_fetcher_locked (GstHLSDemux * demux, const gchar * uri)
{
  GstPad *pad;

  if (!gst_uri_is_valid (uri))
    return FALSE;

  GST_DEBUG_OBJECT (demux, "Creating fetcher for the URI:%s", uri);
  demux->fetcher = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
  if (!demux->fetcher)
    return FALSE;

  demux->fetcher_error = FALSE;
  demux->stopping_fetcher = FALSE;
  gst_element_set_bus (GST_ELEMENT (demux->fetcher), demux->fetcher_bus);

  g_object_set (G_OBJECT (demux->fetcher), "location", uri, NULL);
  pad = gst_element_get_static_pad (demux->fetcher, "src");
  if (pad) {
    gst_pad_link (pad, demux->fetcherpad);
    gst_object_unref (pad);
  }
  return TRUE;
}

static void
gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose)
{
  demux->need_cache = TRUE;
  demux->thread_return = FALSE;
  demux->accumulated_delay = 0;
  demux->end_of_playlist = FALSE;
  demux->cancelled = FALSE;
  demux->do_typefind = TRUE;

  if (demux->input_caps) {
    gst_caps_unref (demux->input_caps);
    demux->input_caps = NULL;
  }

  if (demux->playlist) {
    gst_buffer_unref (demux->playlist);
    demux->playlist = NULL;
  }

  gst_adapter_clear (demux->download);

  if (demux->client) {
    gst_m3u8_client_free (demux->client);
    demux->client = NULL;
  }

  if (!dispose) {
    demux->client = gst_m3u8_client_new ("");
  }

  while (!g_queue_is_empty (demux->queue)) {
    GstBuffer *buf = g_queue_pop_head (demux->queue);
    gst_buffer_unref (buf);
  }
  g_queue_clear (demux->queue);

  demux->position = 0;
  demux->position_shift = 0;
  demux->need_segment = TRUE;
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

static gboolean
gst_hls_demux_update_thread (GstHLSDemux * demux)
{
  /* Loop for the updates. It's started when the first fragments are cached and
   * schedules the next update of the playlist (for lives sources) and the next
   * update of fragments. When a new fragment is downloaded, it compares the
   * download time with the next scheduled update to check if we can or should
   * switch to a different bitrate */

  g_mutex_lock (demux->thread_lock);
  GST_DEBUG_OBJECT (demux, "Started updates thread");
  while (TRUE) {
    /* block until the next scheduled update or the signal to quit this thread */
    if (g_cond_timed_wait (demux->thread_cond, demux->thread_lock,
            &demux->next_update)) {
      goto quit;
    }

    /* update the playlist for live sources */
    if (gst_m3u8_client_is_live (demux->client)) {
      if (!gst_hls_demux_update_playlist (demux)) {
        demux->client->update_failed_count++;
        if (demux->client->update_failed_count < DEFAULT_FAILED_COUNT) {
          GST_WARNING_OBJECT (demux, "Could not update the playlist");
          gst_hls_demux_schedule (demux);
          continue;
        } else {
          GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
              ("Could not update the playlist"), (NULL));
          goto quit;
        }
      }
    }

    /* schedule the next update */
    gst_hls_demux_schedule (demux);

    /* if it's a live source and the playlist couldn't be updated, there aren't
     * more fragments in the playlist, so we just wait for the next schedulled
     * update */
    if (gst_m3u8_client_is_live (demux->client) &&
        demux->client->update_failed_count > 0) {
      GST_WARNING_OBJECT (demux,
          "The playlist hasn't been updated, failed count is %d",
          demux->client->update_failed_count);
      continue;
    }

    /* fetch the next fragment */
    if (g_queue_is_empty (demux->queue)) {
      if (!gst_hls_demux_get_next_fragment (demux)) {
        if (!demux->end_of_playlist && !demux->cancelled) {
          demux->client->update_failed_count++;
          if (demux->client->update_failed_count < DEFAULT_FAILED_COUNT) {
            GST_WARNING_OBJECT (demux, "Could not fetch the next fragment");
            continue;
          } else {
            GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
                ("Could not fetch the next fragment"), (NULL));
            goto quit;
          }
        }
      } else {
        demux->client->update_failed_count = 0;
      }

      /* try to switch to another bitrate if needed */
      gst_hls_demux_switch_playlist (demux);
    }
  }

quit:
  {
    GST_DEBUG_OBJECT (demux, "Stopped updates thread");
    demux->updates_thread = NULL;
    g_mutex_unlock (demux->thread_lock);
    return TRUE;
  }
}


static void
gst_hls_demux_stop_update (GstHLSDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "Stopping updates thread");
  while (demux->updates_thread) {
    g_mutex_lock (demux->thread_lock);
    g_cond_signal (demux->thread_cond);
    g_mutex_unlock (demux->thread_lock);
  }
}

static gboolean
gst_hls_demux_start_update (GstHLSDemux * demux)
{
  GError *error;

  /* creates a new thread for the updates */
  g_mutex_lock (demux->thread_lock);
  if (demux->updates_thread == NULL) {
    GST_DEBUG_OBJECT (demux, "Starting updates thread");
    demux->updates_thread = g_thread_create (
        (GThreadFunc) gst_hls_demux_update_thread, demux, FALSE, &error);
  }
  g_mutex_unlock (demux->thread_lock);
  return (error != NULL);
}

static gboolean
gst_hls_demux_cache_fragments (GstHLSDemux * demux)
{
  gint i;

  /* If this playlist is a variant playlist, select the first one
   * and update it */
  if (gst_m3u8_client_has_variant_playlist (demux->client)) {
    GstM3U8 *child = NULL;

    GST_M3U8_CLIENT_LOCK (demux->client);
    child = demux->client->main->current_variant->data;
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    gst_m3u8_client_set_current (demux->client, child);
    if (!gst_hls_demux_update_playlist (demux)) {
      GST_ERROR_OBJECT (demux, "Could not fetch the child playlist %s",
          child->uri);
      return FALSE;
    }
  }

  /* If it's a live source, set the sequence number to the end of the list
   * and substract the 'fragmets_cache' to start from the last fragment*/
  if (gst_m3u8_client_is_live (demux->client)) {
    GST_M3U8_CLIENT_LOCK (demux->client);
    demux->client->sequence += g_list_length (demux->client->current->files);
    if (demux->client->sequence >= demux->fragments_cache)
      demux->client->sequence -= demux->fragments_cache;
    else
      demux->client->sequence = 0;
    gst_m3u8_client_get_current_position (demux->client, &demux->position);
    GST_M3U8_CLIENT_UNLOCK (demux->client);
  } else {
    GstClockTime duration = gst_m3u8_client_get_duration (demux->client);

    GST_DEBUG_OBJECT (demux, "Sending duration message : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (duration));
    if (duration != GST_CLOCK_TIME_NONE)
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_duration (GST_OBJECT (demux),
              GST_FORMAT_TIME, duration));
  }

  /* Cache the first fragments */
  for (i = 0; i < demux->fragments_cache; i++) {
    gst_element_post_message (GST_ELEMENT (demux),
        gst_message_new_buffering (GST_OBJECT (demux),
            100 * i / demux->fragments_cache));
    g_get_current_time (&demux->next_update);
    g_time_val_add (&demux->next_update,
        gst_m3u8_client_get_target_duration (demux->client)
        / GST_SECOND * G_USEC_PER_SEC);
    if (!gst_hls_demux_get_next_fragment (demux)) {
      if (!demux->cancelled)
        GST_ERROR_OBJECT (demux, "Error caching the first fragments");
      return FALSE;
    }
    /* make sure we stop caching fragments if something cancelled it */
    if (demux->cancelled)
      return FALSE;
    gst_hls_demux_switch_playlist (demux);
  }
  gst_element_post_message (GST_ELEMENT (demux),
      gst_message_new_buffering (GST_OBJECT (demux), 100));

  g_get_current_time (&demux->next_update);

  demux->need_cache = FALSE;
  return TRUE;
}

static gboolean
gst_hls_demux_fetch_location (GstHLSDemux * demux, const gchar * uri)
{
  GstStateChangeReturn ret;
  gboolean bret = FALSE;

  g_mutex_lock (demux->fetcher_lock);

  while (demux->fetcher)
    g_cond_wait (demux->fetcher_cond, demux->fetcher_lock);

  if (demux->cancelled)
    goto quit;

  if (!gst_hls_demux_make_fetcher_locked (demux, uri)) {
    goto uri_error;
  }

  ret = gst_element_set_state (demux->fetcher, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto state_change_error;

  /* wait until we have fetched the uri */
  GST_DEBUG_OBJECT (demux, "Waiting to fetch the URI");
  g_cond_wait (demux->fetcher_cond, demux->fetcher_lock);

  gst_hls_demux_stop_fetcher_locked (demux, FALSE);

  if (!demux->fetcher_error && gst_adapter_available (demux->download)) {
    GST_INFO_OBJECT (demux, "URI fetched successfully");
    bret = TRUE;
  }
  goto quit;

uri_error:
  {
    GST_ELEMENT_ERROR (demux, RESOURCE, OPEN_READ,
        ("Could not create an element to fetch the given URI."), ("URI: \"%s\"",
            uri));
    bret = FALSE;
    goto quit;
  }

state_change_error:
  {
    GST_ELEMENT_ERROR (demux, CORE, STATE_CHANGE,
        ("Error changing state of the fetcher element."), (NULL));
    bret = FALSE;
    goto quit;
  }

quit:
  {
    /* Unlock any other fetcher that might be waiting */
    g_cond_broadcast (demux->fetcher_cond);
    g_mutex_unlock (demux->fetcher_lock);
    return bret;
  }
}

static gchar *
gst_hls_src_buf_to_utf8_playlist (gchar * data, guint size)
{
  gchar *playlist;

  if (!g_utf8_validate (data, size, NULL))
    return NULL;

  /* alloc size + 1 to end with a null character */
  playlist = g_malloc0 (size + 1);
  memcpy (playlist, data, size + 1);
  return playlist;
}

static gboolean
gst_hls_demux_update_playlist (GstHLSDemux * demux)
{
  const guint8 *data;
  gchar *playlist;
  guint avail;
  const gchar *uri = gst_m3u8_client_get_current_uri (demux->client);

  GST_INFO_OBJECT (demux, "Updating the playlist %s", uri);
  if (!gst_hls_demux_fetch_location (demux, uri))
    return FALSE;

  avail = gst_adapter_available (demux->download);
  data = gst_adapter_peek (demux->download, avail);
  playlist = gst_hls_src_buf_to_utf8_playlist ((gchar *) data, avail);
  gst_adapter_clear (demux->download);
  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't not validate playlist encoding");
    return FALSE;
  }
  gst_m3u8_client_update (demux->client, playlist);
  return TRUE;
}

static gboolean
gst_hls_demux_change_playlist (GstHLSDemux * demux, gboolean is_fast)
{
  GList *list;
  GstStructure *s;
  gint new_bandwidth;

  GST_M3U8_CLIENT_LOCK (demux->client);
  if (is_fast)
    list = g_list_next (demux->client->main->current_variant);
  else
    list = g_list_previous (demux->client->main->current_variant);

  /* Don't do anything else if the playlist is the same */
  if (!list || list->data == demux->client->current) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }

  demux->client->main->current_variant = list;
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_m3u8_client_set_current (demux->client, list->data);

  GST_M3U8_CLIENT_LOCK (demux->client);
  new_bandwidth = demux->client->current->bandwidth;
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  gst_hls_demux_update_playlist (demux);
  GST_INFO_OBJECT (demux, "Client is %s, switching to bitrate %d",
      is_fast ? "fast" : "slow", new_bandwidth);

  s = gst_structure_new ("playlist",
      "uri", G_TYPE_STRING, gst_m3u8_client_get_current_uri (demux->client),
      "bitrate", G_TYPE_INT, new_bandwidth, NULL);
  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux), s));

  /* Force typefinding since we might have changed media type */
  demux->do_typefind = TRUE;

  return TRUE;
}

static gboolean
gst_hls_demux_schedule (GstHLSDemux * demux)
{
  gfloat update_factor;
  gint count;

  /* As defined in §6.3.4. Reloading the Playlist file:
   * "If the client reloads a Playlist file and finds that it has not
   * changed then it MUST wait for a period of time before retrying.  The
   * minimum delay is a multiple of the target duration.  This multiple is
   * 0.5 for the first attempt, 1.5 for the second, and 3.0 thereafter."
   */
  count = demux->client->update_failed_count;
  if (count < 3)
    update_factor = update_interval_factor[count];
  else
    update_factor = update_interval_factor[3];

  /* schedule the next update using the target duration field of the
   * playlist */
  g_time_val_add (&demux->next_update,
      gst_m3u8_client_get_target_duration (demux->client)
      / GST_SECOND * G_USEC_PER_SEC * update_factor);
  GST_DEBUG_OBJECT (demux, "Next update scheduled at %s",
      g_time_val_to_iso8601 (&demux->next_update));

  return TRUE;
}

static gboolean
gst_hls_demux_switch_playlist (GstHLSDemux * demux)
{
  GTimeVal now;
  gint64 diff, limit;

  g_get_current_time (&now);
  GST_M3U8_CLIENT_LOCK (demux->client);
  if (!demux->client->main->lists) {
    GST_M3U8_CLIENT_UNLOCK (demux->client);
    return TRUE;
  }
  GST_M3U8_CLIENT_UNLOCK (demux->client);

  /* compare the time when the fragment was downloaded with the time when it was
   * scheduled */
  diff = (GST_TIMEVAL_TO_TIME (demux->next_update) - GST_TIMEVAL_TO_TIME (now));
  limit = gst_m3u8_client_get_target_duration (demux->client)
      * demux->bitrate_switch_tol;

  GST_DEBUG ("diff:%s%" GST_TIME_FORMAT ", limit:%" GST_TIME_FORMAT,
      diff < 0 ? "-" : " ", GST_TIME_ARGS (ABS (diff)), GST_TIME_ARGS (limit));

  /* if we are on time switch to a higher bitrate */
  if (diff > limit) {
    while (diff > limit) {
      gst_hls_demux_change_playlist (demux, TRUE);
      diff -= limit;
    }
    demux->accumulated_delay = 0;
  } else if (diff < 0) {
    /* if the client is too slow wait until it has accumulated a certain delay to
     * switch to a lower bitrate */
    demux->accumulated_delay -= diff;
    if (demux->accumulated_delay >= limit) {
      while (demux->accumulated_delay >= limit) {
        gst_hls_demux_change_playlist (demux, FALSE);
        demux->accumulated_delay -= limit;
      }
      demux->accumulated_delay = 0;
    }
  }
  return TRUE;
}

static gboolean
gst_hls_demux_get_next_fragment (GstHLSDemux * demux)
{
  GstBuffer *buf;
  guint avail;
  const gchar *next_fragment_uri;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;

  if (!gst_m3u8_client_get_next_fragment (demux->client, &discont,
          &next_fragment_uri, &duration, &timestamp)) {
    GST_INFO_OBJECT (demux, "This playlist doesn't contain more fragments");
    demux->end_of_playlist = TRUE;
    gst_task_start (demux->task);
    return FALSE;
  }

  GST_INFO_OBJECT (demux, "Fetching next fragment %s", next_fragment_uri);

  if (!gst_hls_demux_fetch_location (demux, next_fragment_uri)) {
    /* FIXME: The gst_m3u8_get_next_fragment increments the sequence number
       but another thread might call get_next_fragment and this decrement
       will not redownload the failed fragment, but might duplicate the
       download of a succeeded fragment
     */
    g_atomic_int_add (&demux->client->sequence, -1);
    return FALSE;
  }

  avail = gst_adapter_available (demux->download);
  buf = gst_adapter_take_buffer (demux->download, avail);
  GST_BUFFER_DURATION (buf) = duration;
  GST_BUFFER_TIMESTAMP (buf) = timestamp;

  /* We actually need to do this every time we switch bitrate */
  if (G_UNLIKELY (demux->do_typefind)) {
    GstCaps *caps = gst_type_find_helper_for_buffer (NULL, buf, NULL);

    if (!demux->input_caps || !gst_caps_is_equal (caps, demux->input_caps)) {
      gst_caps_replace (&demux->input_caps, caps);
      /* gst_pad_set_caps (demux->srcpad, demux->input_caps); */
      GST_INFO_OBJECT (demux, "Input source caps: %" GST_PTR_FORMAT,
          demux->input_caps);
      demux->do_typefind = FALSE;
    } else
      gst_caps_unref (caps);
  }
  gst_buffer_set_caps (buf, demux->input_caps);

  if (discont) {
    GST_DEBUG_OBJECT (demux, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  }

  g_queue_push_tail (demux->queue, buf);
  gst_task_start (demux->task);
  gst_adapter_clear (demux->download);
  return TRUE;
}
