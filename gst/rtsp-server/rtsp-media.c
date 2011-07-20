/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include <string.h>
#include <stdlib.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include "rtsp-funnel.h"
#include "rtsp-media.h"

#define DEFAULT_SHARED         FALSE
#define DEFAULT_REUSABLE       FALSE
#define DEFAULT_PROTOCOLS      GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_TCP
//#define DEFAULT_PROTOCOLS      GST_RTSP_LOWER_TRANS_UDP_MCAST
#define DEFAULT_EOS_SHUTDOWN   FALSE
#define DEFAULT_BUFFER_SIZE    0x80000

/* define to dump received RTCP packets */
#undef DUMP_STATS

enum
{
  PROP_0,
  PROP_SHARED,
  PROP_REUSABLE,
  PROP_PROTOCOLS,
  PROP_EOS_SHUTDOWN,
  PROP_BUFFER_SIZE,
  PROP_LAST
};

enum
{
  SIGNAL_PREPARED,
  SIGNAL_UNPREPARED,
  SIGNAL_NEW_STATE,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_media_debug);
#define GST_CAT_DEFAULT rtsp_media_debug

static GQuark ssrc_stream_map_key;

static void gst_rtsp_media_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_finalize (GObject * obj);

static gpointer do_loop (GstRTSPMediaClass * klass);
static gboolean default_handle_message (GstRTSPMedia * media,
    GstMessage * message);
static gboolean default_unprepare (GstRTSPMedia * media);
static void unlock_streams (GstRTSPMedia * media);

static guint gst_rtsp_media_signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (GstRTSPMedia, gst_rtsp_media, G_TYPE_OBJECT);

static void
gst_rtsp_media_class_init (GstRTSPMediaClass * klass)
{
  GObjectClass *gobject_class;
  GError *error = NULL;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_media_get_property;
  gobject_class->set_property = gst_rtsp_media_set_property;
  gobject_class->finalize = gst_rtsp_media_finalize;

  g_object_class_install_property (gobject_class, PROP_SHARED,
      g_param_spec_boolean ("shared", "Shared",
          "If this media pipeline can be shared", DEFAULT_SHARED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REUSABLE,
      g_param_spec_boolean ("reusable", "Reusable",
          "If this media pipeline can be reused after an unprepare",
          DEFAULT_REUSABLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols",
          "Allowed lower transport protocols", GST_TYPE_RTSP_LOWER_TRANS,
          DEFAULT_PROTOCOLS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EOS_SHUTDOWN,
      g_param_spec_boolean ("eos-shutdown", "EOS Shutdown",
          "Send an EOS event to the pipeline before unpreparing",
          DEFAULT_EOS_SHUTDOWN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "Buffer Size",
          "The kernel UDP buffer size to use", 0, G_MAXUINT,
          DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_media_signals[SIGNAL_PREPARED] =
      g_signal_new ("prepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, prepared), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_media_signals[SIGNAL_UNPREPARED] =
      g_signal_new ("unprepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, unprepared), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_media_signals[SIGNAL_NEW_STATE] =
      g_signal_new ("new-state", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, new_state), NULL, NULL,
      g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 0, G_TYPE_INT);

  klass->context = g_main_context_new ();
  klass->loop = g_main_loop_new (klass->context, TRUE);

  GST_DEBUG_CATEGORY_INIT (rtsp_media_debug, "rtspmedia", 0, "GstRTSPMedia");

  klass->thread = g_thread_create ((GThreadFunc) do_loop, klass, TRUE, &error);
  if (error != NULL) {
    g_critical ("could not start bus thread: %s", error->message);
  }
  klass->handle_message = default_handle_message;
  klass->unprepare = default_unprepare;

  ssrc_stream_map_key = g_quark_from_static_string ("GstRTSPServer.stream");

  gst_element_register (NULL, "rtspfunnel", GST_RANK_NONE, RTSP_TYPE_FUNNEL);

}

static void
gst_rtsp_media_init (GstRTSPMedia * media)
{
  media->streams = g_array_new (FALSE, TRUE, sizeof (GstRTSPMediaStream *));
  media->lock = g_mutex_new ();
  media->cond = g_cond_new ();

  media->shared = DEFAULT_SHARED;
  media->reusable = DEFAULT_REUSABLE;
  media->protocols = DEFAULT_PROTOCOLS;
  media->eos_shutdown = DEFAULT_EOS_SHUTDOWN;
  media->buffer_size = DEFAULT_BUFFER_SIZE;
}

void
gst_rtsp_media_trans_cleanup (GstRTSPMediaTrans * trans)
{
  if (trans->transport) {
    gst_rtsp_transport_free (trans->transport);
    trans->transport = NULL;
  }
  if (trans->rtpsource) {
    g_object_set_qdata (trans->rtpsource, ssrc_stream_map_key, NULL);
    trans->rtpsource = NULL;
  }
}

static void
gst_rtsp_media_stream_free (GstRTSPMediaStream * stream)
{
  if (stream->session)
    g_object_unref (stream->session);

  if (stream->caps)
    gst_caps_unref (stream->caps);

  if (stream->send_rtp_sink)
    gst_object_unref (stream->send_rtp_sink);
  if (stream->send_rtp_src)
    gst_object_unref (stream->send_rtp_src);
  if (stream->send_rtcp_src)
    gst_object_unref (stream->send_rtcp_src);
  if (stream->recv_rtcp_sink)
    gst_object_unref (stream->recv_rtcp_sink);
  if (stream->recv_rtp_sink)
    gst_object_unref (stream->recv_rtp_sink);

  g_list_free (stream->transports);

  g_free (stream);
}

static void
gst_rtsp_media_finalize (GObject * obj)
{
  GstRTSPMedia *media;
  guint i;

  media = GST_RTSP_MEDIA (obj);

  GST_INFO ("finalize media %p", media);

  if (media->pipeline) {
    unlock_streams (media);
    gst_element_set_state (media->pipeline, GST_STATE_NULL);
    gst_object_unref (media->pipeline);
  }

  for (i = 0; i < media->streams->len; i++) {
    GstRTSPMediaStream *stream;

    stream = g_array_index (media->streams, GstRTSPMediaStream *, i);

    gst_rtsp_media_stream_free (stream);
  }
  g_array_free (media->streams, TRUE);

  g_list_foreach (media->dynamic, (GFunc) gst_object_unref, NULL);
  g_list_free (media->dynamic);

  if (media->source) {
    g_source_destroy (media->source);
    g_source_unref (media->source);
  }
  g_mutex_free (media->lock);
  g_cond_free (media->cond);

  G_OBJECT_CLASS (gst_rtsp_media_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPMedia *media = GST_RTSP_MEDIA (object);

  switch (propid) {
    case PROP_SHARED:
      g_value_set_boolean (value, gst_rtsp_media_is_shared (media));
      break;
    case PROP_REUSABLE:
      g_value_set_boolean (value, gst_rtsp_media_is_reusable (media));
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, gst_rtsp_media_get_protocols (media));
      break;
    case PROP_EOS_SHUTDOWN:
      g_value_set_boolean (value, gst_rtsp_media_is_eos_shutdown (media));
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value, gst_rtsp_media_get_buffer_size (media));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_media_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPMedia *media = GST_RTSP_MEDIA (object);

  switch (propid) {
    case PROP_SHARED:
      gst_rtsp_media_set_shared (media, g_value_get_boolean (value));
      break;
    case PROP_REUSABLE:
      gst_rtsp_media_set_reusable (media, g_value_get_boolean (value));
      break;
    case PROP_PROTOCOLS:
      gst_rtsp_media_set_protocols (media, g_value_get_flags (value));
      break;
    case PROP_EOS_SHUTDOWN:
      gst_rtsp_media_set_eos_shutdown (media, g_value_get_boolean (value));
      break;
    case PROP_BUFFER_SIZE:
      gst_rtsp_media_set_buffer_size (media, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static gpointer
do_loop (GstRTSPMediaClass * klass)
{
  GST_INFO ("enter mainloop");
  g_main_loop_run (klass->loop);
  GST_INFO ("exit mainloop");

  return NULL;
}

static void
collect_media_stats (GstRTSPMedia * media)
{
  GstFormat format;
  gint64 position, duration;

  media->range.unit = GST_RTSP_RANGE_NPT;

  if (media->is_live) {
    media->range.min.type = GST_RTSP_TIME_NOW;
    media->range.min.seconds = -1;
    media->range.max.type = GST_RTSP_TIME_END;
    media->range.max.seconds = -1;
  } else {
    /* get the position */
    format = GST_FORMAT_TIME;
    if (!gst_element_query_position (media->pipeline, &format, &position)) {
      GST_INFO ("position query failed");
      position = 0;
    }

    /* get the duration */
    format = GST_FORMAT_TIME;
    if (!gst_element_query_duration (media->pipeline, &format, &duration)) {
      GST_INFO ("duration query failed");
      duration = -1;
    }

    GST_INFO ("stats: position %" GST_TIME_FORMAT ", duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (position), GST_TIME_ARGS (duration));

    if (position == -1) {
      media->range.min.type = GST_RTSP_TIME_NOW;
      media->range.min.seconds = -1;
    } else {
      media->range.min.type = GST_RTSP_TIME_SECONDS;
      media->range.min.seconds = ((gdouble) position) / GST_SECOND;
    }
    if (duration == -1) {
      media->range.max.type = GST_RTSP_TIME_END;
      media->range.max.seconds = -1;
    } else {
      media->range.max.type = GST_RTSP_TIME_SECONDS;
      media->range.max.seconds = ((gdouble) duration) / GST_SECOND;
    }
  }
}

/**
 * gst_rtsp_media_new:
 *
 * Create a new #GstRTSPMedia instance. The #GstRTSPMedia object contains the
 * element to produde RTP data for one or more related (audio/video/..) 
 * streams.
 *
 * Returns: a new #GstRTSPMedia object.
 */
GstRTSPMedia *
gst_rtsp_media_new (void)
{
  GstRTSPMedia *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA, NULL);

  return result;
}

/**
 * gst_rtsp_media_set_shared:
 * @media: a #GstRTSPMedia
 * @shared: the new value
 *
 * Set or unset if the pipeline for @media can be shared will multiple clients.
 * When @shared is %TRUE, client requests for this media will share the media
 * pipeline.
 */
void
gst_rtsp_media_set_shared (GstRTSPMedia * media, gboolean shared)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  media->shared = shared;
}

/**
 * gst_rtsp_media_is_shared:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media can be shared between multiple clients.
 *
 * Returns: %TRUE if the media can be shared between clients.
 */
gboolean
gst_rtsp_media_is_shared (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  return media->shared;
}

/**
 * gst_rtsp_media_set_reusable:
 * @media: a #GstRTSPMedia
 * @reusable: the new value
 *
 * Set or unset if the pipeline for @media can be reused after the pipeline has
 * been unprepared.
 */
void
gst_rtsp_media_set_reusable (GstRTSPMedia * media, gboolean reusable)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  media->reusable = reusable;
}

/**
 * gst_rtsp_media_is_reusable:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media can be reused after an unprepare.
 *
 * Returns: %TRUE if the media can be reused
 */
gboolean
gst_rtsp_media_is_reusable (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  return media->reusable;
}

/**
 * gst_rtsp_media_set_protocols:
 * @media: a #GstRTSPMedia
 * @protocols: the new flags
 *
 * Configure the allowed lower transport for @media.
 */
void
gst_rtsp_media_set_protocols (GstRTSPMedia * media, GstRTSPLowerTrans protocols)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  media->protocols = protocols;
}

/**
 * gst_rtsp_media_get_protocols:
 * @media: a #GstRTSPMedia
 *
 * Get the allowed protocols of @media.
 *
 * Returns: a #GstRTSPLowerTrans
 */
GstRTSPLowerTrans
gst_rtsp_media_get_protocols (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media),
      GST_RTSP_LOWER_TRANS_UNKNOWN);

  return media->protocols;
}

/**
 * gst_rtsp_media_set_eos_shutdown:
 * @media: a #GstRTSPMedia
 * @eos_shutdown: the new value
 *
 * Set or unset if an EOS event will be sent to the pipeline for @media before
 * it is unprepared.
 */
void
gst_rtsp_media_set_eos_shutdown (GstRTSPMedia * media, gboolean eos_shutdown)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  media->eos_shutdown = eos_shutdown;
}

/**
 * gst_rtsp_media_is_eos_shutdown:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media will send an EOS down the pipeline before
 * unpreparing.
 *
 * Returns: %TRUE if the media will send EOS before unpreparing.
 */
gboolean
gst_rtsp_media_is_eos_shutdown (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  return media->eos_shutdown;
}

/**
 * gst_rtsp_media_set_buffer_size:
 * @media: a #GstRTSPMedia
 * @size: the new value
 *
 * Set the kernel UDP buffer size.
 */
void
gst_rtsp_media_set_buffer_size (GstRTSPMedia * media, guint size)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  media->buffer_size = size;
}

/**
 * gst_rtsp_media_get_buffer_size:
 * @media: a #GstRTSPMedia
 *
 * Get the kernel UDP buffer size.
 *
 * Returns: the kernel UDP buffer size.
 */
guint
gst_rtsp_media_get_buffer_size (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  return media->buffer_size;
}

/**
 * gst_rtsp_media_set_auth:
 * @media: a #GstRTSPMedia
 * @auth: a #GstRTSPAuth
 *
 * configure @auth to be used as the authentication manager of @media.
 */
void
gst_rtsp_media_set_auth (GstRTSPMedia * media, GstRTSPAuth * auth)
{
  GstRTSPAuth *old;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  old = media->auth;

  if (old != auth) {
    if (auth)
      g_object_ref (auth);
    media->auth = auth;
    if (old)
      g_object_unref (old);
  }
}

/**
 * gst_rtsp_media_get_auth:
 * @media: a #GstRTSPMedia
 *
 * Get the #GstRTSPAuth used as the authentication manager of @media.
 *
 * Returns: the #GstRTSPAuth of @media. g_object_unref() after
 * usage.
 */
GstRTSPAuth *
gst_rtsp_media_get_auth (GstRTSPMedia * media)
{
  GstRTSPAuth *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  if ((result = media->auth))
    g_object_ref (result);

  return result;
}


/**
 * gst_rtsp_media_n_streams:
 * @media: a #GstRTSPMedia
 *
 * Get the number of streams in this media.
 *
 * Returns: The number of streams.
 */
guint
gst_rtsp_media_n_streams (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), 0);

  return media->streams->len;
}

/**
 * gst_rtsp_media_get_stream:
 * @media: a #GstRTSPMedia
 * @idx: the stream index
 *
 * Retrieve the stream with index @idx from @media.
 *
 * Returns: the #GstRTSPMediaStream at index @idx or %NULL when a stream with
 * that index did not exist.
 */
GstRTSPMediaStream *
gst_rtsp_media_get_stream (GstRTSPMedia * media, guint idx)
{
  GstRTSPMediaStream *res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  if (idx < media->streams->len)
    res = g_array_index (media->streams, GstRTSPMediaStream *, idx);
  else
    res = NULL;

  return res;
}

/**
 * gst_rtsp_media_get_range_string:
 * @media: a #GstRTSPMedia
 * @play: for the PLAY request
 *
 * Get the current range as a string.
 *
 * Returns: The range as a string, g_free() after usage.
 */
gchar *
gst_rtsp_media_get_range_string (GstRTSPMedia * media, gboolean play)
{
  gchar *result;
  GstRTSPTimeRange range;

  /* make copy */
  range = media->range;

  if (!play && media->active > 0) {
    range.min.type = GST_RTSP_TIME_NOW;
    range.min.seconds = -1;
  }

  result = gst_rtsp_range_to_string (&range);

  return result;
}

/**
 * gst_rtsp_media_seek:
 * @media: a #GstRTSPMedia
 * @range: a #GstRTSPTimeRange
 *
 * Seek the pipeline to @range.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_seek (GstRTSPMedia * media, GstRTSPTimeRange * range)
{
  GstSeekFlags flags;
  gboolean res;
  gint64 start, stop;
  GstSeekType start_type, stop_type;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (range != NULL, FALSE);

  if (range->unit != GST_RTSP_RANGE_NPT)
    goto not_supported;

  /* depends on the current playing state of the pipeline. We might need to
   * queue this until we get EOS. */
  flags = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_KEY_UNIT;

  start_type = stop_type = GST_SEEK_TYPE_NONE;

  switch (range->min.type) {
    case GST_RTSP_TIME_NOW:
      start = -1;
      break;
    case GST_RTSP_TIME_SECONDS:
      /* only seek when something changed */
      if (media->range.min.seconds == range->min.seconds) {
        start = -1;
      } else {
        start = range->min.seconds * GST_SECOND;
        start_type = GST_SEEK_TYPE_SET;
      }
      break;
    case GST_RTSP_TIME_END:
    default:
      goto weird_type;
  }
  switch (range->max.type) {
    case GST_RTSP_TIME_SECONDS:
      /* only seek when something changed */
      if (media->range.max.seconds == range->max.seconds) {
        stop = -1;
      } else {
        stop = range->max.seconds * GST_SECOND;
        stop_type = GST_SEEK_TYPE_SET;
      }
      break;
    case GST_RTSP_TIME_END:
      stop = -1;
      stop_type = GST_SEEK_TYPE_SET;
      break;
    case GST_RTSP_TIME_NOW:
    default:
      goto weird_type;
  }

  if (start != -1 || stop != -1) {
    GST_INFO ("seeking to %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

    res = gst_element_seek (media->pipeline, 1.0, GST_FORMAT_TIME,
        flags, start_type, start, stop_type, stop);

    /* and block for the seek to complete */
    GST_INFO ("done seeking %d", res);
    gst_element_get_state (media->pipeline, NULL, NULL, -1);
    GST_INFO ("prerolled again");

    collect_media_stats (media);
  } else {
    GST_INFO ("no seek needed");
    res = TRUE;
  }

  return res;

  /* ERRORS */
not_supported:
  {
    GST_WARNING ("seek unit %d not supported", range->unit);
    return FALSE;
  }
weird_type:
  {
    GST_WARNING ("weird range type %d not supported", range->min.type);
    return FALSE;
  }
}

/**
 * gst_rtsp_media_stream_rtp:
 * @stream: a #GstRTSPMediaStream
 * @buffer: a #GstBuffer
 *
 * Handle an RTP buffer for the stream. This method is usually called when a
 * message has been received from a client using the TCP transport.
 *
 * This function takes ownership of @buffer.
 *
 * Returns: a GstFlowReturn.
 */
GstFlowReturn
gst_rtsp_media_stream_rtp (GstRTSPMediaStream * stream, GstBuffer * buffer)
{
  GstFlowReturn ret;

  ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (stream->appsrc[0]), buffer);

  return ret;
}

/**
 * gst_rtsp_media_stream_rtcp:
 * @stream: a #GstRTSPMediaStream
 * @buffer: a #GstBuffer
 *
 * Handle an RTCP buffer for the stream. This method is usually called when a
 * message has been received from a client using the TCP transport.
 *
 * This function takes ownership of @buffer.
 *
 * Returns: a GstFlowReturn.
 */
GstFlowReturn
gst_rtsp_media_stream_rtcp (GstRTSPMediaStream * stream, GstBuffer * buffer)
{
  GstFlowReturn ret;

  ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (stream->appsrc[1]), buffer);

  return ret;
}

/* Allocate the udp ports and sockets */
static gboolean
alloc_udp_ports (GstRTSPMedia * media, GstRTSPMediaStream * stream)
{
  GstStateChangeReturn ret;
  GstElement *udpsrc0, *udpsrc1;
  GstElement *udpsink0, *udpsink1;
  gint tmp_rtp, tmp_rtcp;
  guint count;
  gint rtpport, rtcpport, sockfd;
  const gchar *host;

  udpsrc0 = NULL;
  udpsrc1 = NULL;
  udpsink0 = NULL;
  udpsink1 = NULL;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;

  if (media->is_ipv6)
    host = "udp://[::0]";
  else
    host = "udp://0.0.0.0";

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  udpsrc0 = gst_element_make_from_uri (GST_URI_SRC, host, NULL);
  if (udpsrc0 == NULL)
    goto no_udp_protocol;
  g_object_set (G_OBJECT (udpsrc0), "port", tmp_rtp, NULL);

  ret = gst_element_set_state (udpsrc0, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    if (tmp_rtp != 0) {
      tmp_rtp += 2;
      if (++count > 20)
        goto no_ports;

      gst_element_set_state (udpsrc0, GST_STATE_NULL);
      gst_object_unref (udpsrc0);

      goto again;
    }
    goto no_udp_protocol;
  }

  g_object_get (G_OBJECT (udpsrc0), "port", &tmp_rtp, NULL);

  /* check if port is even */
  if ((tmp_rtp & 1) != 0) {
    /* port not even, close and allocate another */
    if (++count > 20)
      goto no_ports;

    gst_element_set_state (udpsrc0, GST_STATE_NULL);
    gst_object_unref (udpsrc0);

    tmp_rtp++;
    goto again;
  }

  /* allocate port+1 for RTCP now */
  udpsrc1 = gst_element_make_from_uri (GST_URI_SRC, host, NULL);
  if (udpsrc1 == NULL)
    goto no_udp_rtcp_protocol;

  /* set port */
  tmp_rtcp = tmp_rtp + 1;
  g_object_set (G_OBJECT (udpsrc1), "port", tmp_rtcp, NULL);

  ret = gst_element_set_state (udpsrc1, GST_STATE_PAUSED);
  /* tmp_rtcp port is busy already : retry to make rtp/rtcp pair */
  if (ret == GST_STATE_CHANGE_FAILURE) {

    if (++count > 20)
      goto no_ports;

    gst_element_set_state (udpsrc0, GST_STATE_NULL);
    gst_object_unref (udpsrc0);

    gst_element_set_state (udpsrc1, GST_STATE_NULL);
    gst_object_unref (udpsrc1);

    tmp_rtp += 2;
    goto again;
  }

  /* all fine, do port check */
  g_object_get (G_OBJECT (udpsrc0), "port", &rtpport, NULL);
  g_object_get (G_OBJECT (udpsrc1), "port", &rtcpport, NULL);

  /* this should not happen... */
  if (rtpport != tmp_rtp || rtcpport != tmp_rtcp)
    goto port_error;

  udpsink0 = gst_element_factory_make ("multiudpsink", NULL);
  if (!udpsink0)
    goto no_udp_protocol;

  g_object_get (G_OBJECT (udpsrc0), "sock", &sockfd, NULL);
  g_object_set (G_OBJECT (udpsink0), "sockfd", sockfd, NULL);
  g_object_set (G_OBJECT (udpsink0), "closefd", FALSE, NULL);

  udpsink1 = gst_element_factory_make ("multiudpsink", NULL);
  if (!udpsink1)
    goto no_udp_protocol;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (udpsink0),
          "send-duplicates")) {
    g_object_set (G_OBJECT (udpsink0), "send-duplicates", FALSE, NULL);
    g_object_set (G_OBJECT (udpsink1), "send-duplicates", FALSE, NULL);
  } else {
    g_warning
        ("old multiudpsink version found without send-duplicates property");
  }

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (udpsink0),
          "buffer-size")) {
    g_object_set (G_OBJECT (udpsink0), "buffer-size", media->buffer_size, NULL);
  } else {
    GST_WARNING ("multiudpsink version found without buffer-size property");
  }

  g_object_get (G_OBJECT (udpsrc1), "sock", &sockfd, NULL);
  g_object_set (G_OBJECT (udpsink1), "sockfd", sockfd, NULL);
  g_object_set (G_OBJECT (udpsink1), "closefd", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "async", FALSE, NULL);

  g_object_set (G_OBJECT (udpsink0), "auto-multicast", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink0), "loop", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "auto-multicast", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "loop", FALSE, NULL);

  /* we keep these elements, we configure all in configure_transport when the
   * server told us to really use the UDP ports. */
  stream->udpsrc[0] = udpsrc0;
  stream->udpsrc[1] = udpsrc1;
  stream->udpsink[0] = udpsink0;
  stream->udpsink[1] = udpsink1;
  stream->server_port.min = rtpport;
  stream->server_port.max = rtcpport;

  return TRUE;

  /* ERRORS */
no_udp_protocol:
  {
    goto cleanup;
  }
no_ports:
  {
    goto cleanup;
  }
no_udp_rtcp_protocol:
  {
    goto cleanup;
  }
port_error:
  {
    goto cleanup;
  }
cleanup:
  {
    if (udpsrc0) {
      gst_element_set_state (udpsrc0, GST_STATE_NULL);
      gst_object_unref (udpsrc0);
    }
    if (udpsrc1) {
      gst_element_set_state (udpsrc1, GST_STATE_NULL);
      gst_object_unref (udpsrc1);
    }
    if (udpsink0) {
      gst_element_set_state (udpsink0, GST_STATE_NULL);
      gst_object_unref (udpsink0);
    }
    if (udpsink1) {
      gst_element_set_state (udpsink1, GST_STATE_NULL);
      gst_object_unref (udpsink1);
    }
    return FALSE;
  }
}

/* executed from streaming thread */
static void
caps_notify (GstPad * pad, GParamSpec * unused, GstRTSPMediaStream * stream)
{
  gchar *capsstr;
  GstCaps *newcaps, *oldcaps;

  if ((newcaps = GST_PAD_CAPS (pad)))
    gst_caps_ref (newcaps);

  oldcaps = stream->caps;
  stream->caps = newcaps;

  if (oldcaps)
    gst_caps_unref (oldcaps);

  capsstr = gst_caps_to_string (newcaps);
  GST_INFO ("stream %p received caps %p, %s", stream, newcaps, capsstr);
  g_free (capsstr);
}

static void
dump_structure (const GstStructure * s)
{
  gchar *sstr;

  sstr = gst_structure_to_string (s);
  GST_INFO ("structure: %s", sstr);
  g_free (sstr);
}

static GstRTSPMediaTrans *
find_transport (GstRTSPMediaStream * stream, const gchar * rtcp_from)
{
  GList *walk;
  GstRTSPMediaTrans *result = NULL;
  const gchar *tmp;
  gchar *dest;
  guint port;

  if (rtcp_from == NULL)
    return NULL;

  tmp = g_strrstr (rtcp_from, ":");
  if (tmp == NULL)
    return NULL;

  port = atoi (tmp + 1);
  dest = g_strndup (rtcp_from, tmp - rtcp_from);

  GST_INFO ("finding %s:%d", dest, port);

  for (walk = stream->transports; walk; walk = g_list_next (walk)) {
    GstRTSPMediaTrans *trans = walk->data;
    gint min, max;

    min = trans->transport->client_port.min;
    max = trans->transport->client_port.max;

    if ((strcmp (trans->transport->destination, dest) == 0) && (min == port
            || max == port)) {
      result = trans;
      break;
    }
  }
  g_free (dest);

  return result;
}

static void
on_new_ssrc (GObject * session, GObject * source, GstRTSPMediaStream * stream)
{
  GstStructure *stats;
  GstRTSPMediaTrans *trans;

  GST_INFO ("%p: new source %p", stream, source);

  /* see if we have a stream to match with the origin of the RTCP packet */
  trans = g_object_get_qdata (source, ssrc_stream_map_key);
  if (trans == NULL) {
    g_object_get (source, "stats", &stats, NULL);
    if (stats) {
      const gchar *rtcp_from;

      dump_structure (stats);

      rtcp_from = gst_structure_get_string (stats, "rtcp-from");
      if ((trans = find_transport (stream, rtcp_from))) {
        GST_INFO ("%p: found transport %p for source  %p", stream, trans,
            source);

        /* keep ref to the source */
        trans->rtpsource = source;

        g_object_set_qdata (source, ssrc_stream_map_key, trans);
      }
      gst_structure_free (stats);
    }
  } else {
    GST_INFO ("%p: source %p for transport %p", stream, source, trans);
  }
}

static void
on_ssrc_sdes (GObject * session, GObject * source, GstRTSPMediaStream * stream)
{
  GST_INFO ("%p: new SDES %p", stream, source);
}

static void
on_ssrc_active (GObject * session, GObject * source,
    GstRTSPMediaStream * stream)
{
  GstRTSPMediaTrans *trans;

  trans = g_object_get_qdata (source, ssrc_stream_map_key);

  GST_INFO ("%p: source %p in transport %p is active", stream, source, trans);

  if (trans && trans->keep_alive)
    trans->keep_alive (trans->ka_user_data);

#ifdef DUMP_STATS
  {
    GstStructure *stats;
    g_object_get (source, "stats", &stats, NULL);
    if (stats) {
      dump_structure (stats);
      gst_structure_free (stats);
    }
  }
#endif
}

static void
on_bye_ssrc (GObject * session, GObject * source, GstRTSPMediaStream * stream)
{
  GST_INFO ("%p: source %p bye", stream, source);
}

static void
on_bye_timeout (GObject * session, GObject * source,
    GstRTSPMediaStream * stream)
{
  GstRTSPMediaTrans *trans;

  GST_INFO ("%p: source %p bye timeout", stream, source);

  if ((trans = g_object_get_qdata (source, ssrc_stream_map_key))) {
    trans->rtpsource = NULL;
    trans->timeout = TRUE;
  }
}

static void
on_timeout (GObject * session, GObject * source, GstRTSPMediaStream * stream)
{
  GstRTSPMediaTrans *trans;

  GST_INFO ("%p: source %p timeout", stream, source);

  if ((trans = g_object_get_qdata (source, ssrc_stream_map_key))) {
    trans->rtpsource = NULL;
    trans->timeout = TRUE;
  }
}

static GstFlowReturn
handle_new_buffer (GstAppSink * sink, gpointer user_data)
{
  GList *walk;
  GstBuffer *buffer;
  GstRTSPMediaStream *stream;

  buffer = gst_app_sink_pull_buffer (sink);
  if (!buffer)
    return GST_FLOW_OK;

  stream = (GstRTSPMediaStream *) user_data;

  for (walk = stream->transports; walk; walk = g_list_next (walk)) {
    GstRTSPMediaTrans *tr = (GstRTSPMediaTrans *) walk->data;

    if (GST_ELEMENT_CAST (sink) == stream->appsink[0]) {
      if (tr->send_rtp)
        tr->send_rtp (buffer, tr->transport->interleaved.min, tr->user_data);
    } else {
      if (tr->send_rtcp)
        tr->send_rtcp (buffer, tr->transport->interleaved.max, tr->user_data);
    }
  }
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static GstFlowReturn
handle_new_buffer_list (GstAppSink * sink, gpointer user_data)
{
  GList *walk;
  GstBufferList *blist;
  GstRTSPMediaStream *stream;

  blist = gst_app_sink_pull_buffer_list (sink);
  if (!blist)
    return GST_FLOW_OK;

  stream = (GstRTSPMediaStream *) user_data;

  for (walk = stream->transports; walk; walk = g_list_next (walk)) {
    GstRTSPMediaTrans *tr = (GstRTSPMediaTrans *) walk->data;

    if (GST_ELEMENT_CAST (sink) == stream->appsink[0]) {
      if (tr->send_rtp_list)
        tr->send_rtp_list (blist, tr->transport->interleaved.min,
            tr->user_data);
    } else {
      if (tr->send_rtcp_list)
        tr->send_rtcp_list (blist, tr->transport->interleaved.max,
            tr->user_data);
    }
  }
  gst_buffer_list_unref (blist);

  return GST_FLOW_OK;
}

static GstAppSinkCallbacks sink_cb = {
  NULL,                         /* not interested in EOS */
  NULL,                         /* not interested in preroll buffers */
  handle_new_buffer,
  handle_new_buffer_list
};

/* prepare the pipeline objects to handle @stream in @media */
static gboolean
setup_stream (GstRTSPMediaStream * stream, guint idx, GstRTSPMedia * media)
{
  gchar *name;
  GstPad *pad, *teepad, *selpad;
  GstPadLinkReturn ret;
  gint i;

  /* allocate udp ports, we will have 4 of them, 2 for receiving RTP/RTCP and 2
   * for sending RTP/RTCP. The sender and receiver ports are shared between the
   * elements */
  if (!alloc_udp_ports (media, stream))
    return FALSE;

  /* add the ports to the pipeline */
  for (i = 0; i < 2; i++) {
    gst_bin_add (GST_BIN_CAST (media->pipeline), stream->udpsink[i]);
    gst_bin_add (GST_BIN_CAST (media->pipeline), stream->udpsrc[i]);
  }

  /* create elements for the TCP transfer */
  for (i = 0; i < 2; i++) {
    stream->appsrc[i] = gst_element_factory_make ("appsrc", NULL);
    stream->appsink[i] = gst_element_factory_make ("appsink", NULL);
    g_object_set (stream->appsink[i], "async", FALSE, "sync", FALSE, NULL);
    g_object_set (stream->appsink[i], "emit-signals", FALSE, NULL);
    g_object_set (stream->appsink[i], "preroll-queue-len", 1, NULL);
    gst_bin_add (GST_BIN_CAST (media->pipeline), stream->appsink[i]);
    gst_bin_add (GST_BIN_CAST (media->pipeline), stream->appsrc[i]);
    gst_app_sink_set_callbacks (GST_APP_SINK_CAST (stream->appsink[i]),
        &sink_cb, stream, NULL);
  }

  /* hook up the stream to the RTP session elements. */
  name = g_strdup_printf ("send_rtp_sink_%d", idx);
  stream->send_rtp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtp_src_%d", idx);
  stream->send_rtp_src = gst_element_get_static_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtcp_src_%d", idx);
  stream->send_rtcp_src = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtcp_sink_%d", idx);
  stream->recv_rtcp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtp_sink_%d", idx);
  stream->recv_rtp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);

  /* get the session */
  g_signal_emit_by_name (media->rtpbin, "get-internal-session", idx,
      &stream->session);

  g_signal_connect (stream->session, "on-new-ssrc", (GCallback) on_new_ssrc,
      stream);
  g_signal_connect (stream->session, "on-ssrc-sdes", (GCallback) on_ssrc_sdes,
      stream);
  g_signal_connect (stream->session, "on-ssrc-active",
      (GCallback) on_ssrc_active, stream);
  g_signal_connect (stream->session, "on-bye-ssrc", (GCallback) on_bye_ssrc,
      stream);
  g_signal_connect (stream->session, "on-bye-timeout",
      (GCallback) on_bye_timeout, stream);
  g_signal_connect (stream->session, "on-timeout", (GCallback) on_timeout,
      stream);

  /* link the RTP pad to the session manager */
  ret = gst_pad_link (stream->srcpad, stream->send_rtp_sink);
  if (ret != GST_PAD_LINK_OK)
    goto link_failed;

  /* make tee for RTP and link to stream */
  stream->tee[0] = gst_element_factory_make ("tee", NULL);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->tee[0]);

  pad = gst_element_get_static_pad (stream->tee[0], "sink");
  gst_pad_link (stream->send_rtp_src, pad);
  gst_object_unref (pad);

  /* link RTP sink, we're pretty sure this will work. */
  teepad = gst_element_get_request_pad (stream->tee[0], "src%d");
  pad = gst_element_get_static_pad (stream->udpsink[0], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  teepad = gst_element_get_request_pad (stream->tee[0], "src%d");
  pad = gst_element_get_static_pad (stream->appsink[0], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  /* make tee for RTCP */
  stream->tee[1] = gst_element_factory_make ("tee", NULL);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->tee[1]);

  pad = gst_element_get_static_pad (stream->tee[1], "sink");
  gst_pad_link (stream->send_rtcp_src, pad);
  gst_object_unref (pad);

  /* link RTCP elements */
  teepad = gst_element_get_request_pad (stream->tee[1], "src%d");
  pad = gst_element_get_static_pad (stream->udpsink[1], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  teepad = gst_element_get_request_pad (stream->tee[1], "src%d");
  pad = gst_element_get_static_pad (stream->appsink[1], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  /* make selector for the RTP receivers */
  stream->selector[0] = gst_element_factory_make ("rtspfunnel", NULL);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->selector[0]);

  pad = gst_element_get_static_pad (stream->selector[0], "src");
  gst_pad_link (pad, stream->recv_rtp_sink);
  gst_object_unref (pad);

  selpad = gst_element_get_request_pad (stream->selector[0], "sink%d");
  pad = gst_element_get_static_pad (stream->udpsrc[0], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);

  selpad = gst_element_get_request_pad (stream->selector[0], "sink%d");
  pad = gst_element_get_static_pad (stream->appsrc[0], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);

  /* make selector for the RTCP receivers */
  stream->selector[1] = gst_element_factory_make ("rtspfunnel", NULL);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->selector[1]);

  pad = gst_element_get_static_pad (stream->selector[1], "src");
  gst_pad_link (pad, stream->recv_rtcp_sink);
  gst_object_unref (pad);

  selpad = gst_element_get_request_pad (stream->selector[1], "sink%d");
  pad = gst_element_get_static_pad (stream->udpsrc[1], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);

  selpad = gst_element_get_request_pad (stream->selector[1], "sink%d");
  pad = gst_element_get_static_pad (stream->appsrc[1], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);

  /* we set and keep these to playing so that they don't cause NO_PREROLL return
   * values */
  gst_element_set_state (stream->udpsrc[0], GST_STATE_PLAYING);
  gst_element_set_state (stream->udpsrc[1], GST_STATE_PLAYING);
  gst_element_set_locked_state (stream->udpsrc[0], TRUE);
  gst_element_set_locked_state (stream->udpsrc[1], TRUE);

  /* be notified of caps changes */
  stream->caps_sig = g_signal_connect (stream->send_rtp_sink, "notify::caps",
      (GCallback) caps_notify, stream);

  stream->prepared = TRUE;

  return TRUE;

  /* ERRORS */
link_failed:
  {
    GST_WARNING ("failed to link stream %d", idx);
    return FALSE;
  }
}

static void
unlock_streams (GstRTSPMedia * media)
{
  guint i, n_streams;

  /* unlock the udp src elements */
  n_streams = gst_rtsp_media_n_streams (media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;

    stream = gst_rtsp_media_get_stream (media, i);

    gst_element_set_locked_state (stream->udpsrc[0], FALSE);
    gst_element_set_locked_state (stream->udpsrc[1], FALSE);
  }
}

static void
gst_rtsp_media_set_status (GstRTSPMedia * media, GstRTSPMediaStatus status)
{
  g_mutex_lock (media->lock);
  /* never overwrite the error status */
  if (media->status != GST_RTSP_MEDIA_STATUS_ERROR)
    media->status = status;
  GST_DEBUG ("setting new status to %d", status);
  g_cond_broadcast (media->cond);
  g_mutex_unlock (media->lock);
}

static GstRTSPMediaStatus
gst_rtsp_media_get_status (GstRTSPMedia * media)
{
  GstRTSPMediaStatus result;
  GTimeVal timeout;

  g_mutex_lock (media->lock);
  g_get_current_time (&timeout);
  g_time_val_add (&timeout, 20 * G_USEC_PER_SEC);
  /* while we are preparing, wait */
  while (media->status == GST_RTSP_MEDIA_STATUS_PREPARING) {
    GST_DEBUG ("waiting for status change");
    if (!g_cond_timed_wait (media->cond, media->lock, &timeout)) {
      GST_DEBUG ("timeout, assuming error status");
      media->status = GST_RTSP_MEDIA_STATUS_ERROR;
    }
  }
  /* could be success or error */
  result = media->status;
  GST_DEBUG ("got status %d", result);
  g_mutex_unlock (media->lock);

  return result;
}

static gboolean
default_handle_message (GstRTSPMedia * media, GstMessage * message)
{
  GstMessageType type;

  type = GST_MESSAGE_TYPE (message);

  switch (type) {
    case GST_MESSAGE_STATE_CHANGED:
      break;
    case GST_MESSAGE_BUFFERING:
    {
      gint percent;

      gst_message_parse_buffering (message, &percent);

      /* no state management needed for live pipelines */
      if (media->is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        media->buffering = FALSE;
        /* if the desired state is playing, go back */
        if (media->target_state == GST_STATE_PLAYING) {
          GST_INFO ("Buffering done, setting pipeline to PLAYING");
          gst_element_set_state (media->pipeline, GST_STATE_PLAYING);
        } else {
          GST_INFO ("Buffering done");
        }
      } else {
        /* buffering busy */
        if (media->buffering == FALSE) {
          if (media->target_state == GST_STATE_PLAYING) {
            /* we were not buffering but PLAYING, PAUSE  the pipeline. */
            GST_INFO ("Buffering, setting pipeline to PAUSED ...");
            gst_element_set_state (media->pipeline, GST_STATE_PAUSED);
          } else {
            GST_INFO ("Buffering ...");
          }
        }
        media->buffering = TRUE;
      }
      break;
    }
    case GST_MESSAGE_LATENCY:
    {
      gst_bin_recalculate_latency (GST_BIN_CAST (media->pipeline));
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      GST_WARNING ("%p: got error %s (%s)", media, gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);

      gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_warning (message, &gerror, &debug);
      GST_WARNING ("%p: got warning %s (%s)", media, gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_ELEMENT:
      break;
    case GST_MESSAGE_STREAM_STATUS:
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (!media->adding) {
        /* when we are dynamically adding pads, the addition of the udpsrc will
         * temporarily produce ASYNC_DONE messages. We have to ignore them and
         * wait for the final ASYNC_DONE after everything prerolled */
        GST_INFO ("%p: got ASYNC_DONE", media);
        collect_media_stats (media);

        gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
      } else {
        GST_INFO ("%p: ignoring ASYNC_DONE", media);
      }
      break;
    case GST_MESSAGE_EOS:
      GST_INFO ("%p: got EOS", media);
      if (media->eos_pending) {
        GST_DEBUG ("shutting down after EOS");
        gst_element_set_state (media->pipeline, GST_STATE_NULL);
        media->eos_pending = FALSE;
        g_object_unref (media);
      }
      break;
    default:
      GST_INFO ("%p: got message type %s", media,
          gst_message_type_get_name (type));
      break;
  }
  return TRUE;
}

static gboolean
bus_message (GstBus * bus, GstMessage * message, GstRTSPMedia * media)
{
  GstRTSPMediaClass *klass;
  gboolean ret;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  if (klass->handle_message)
    ret = klass->handle_message (media, message);
  else
    ret = FALSE;

  return ret;
}

/* called from streaming threads */
static void
pad_added_cb (GstElement * element, GstPad * pad, GstRTSPMedia * media)
{
  GstRTSPMediaStream *stream;
  gchar *name;
  gint i;

  i = media->streams->len + 1;

  GST_INFO ("pad added %s:%s, stream %d", GST_DEBUG_PAD_NAME (pad), i);

  stream = g_new0 (GstRTSPMediaStream, 1);
  stream->payloader = element;

  name = g_strdup_printf ("dynpay%d", i);

  media->adding = TRUE;

  /* ghost the pad of the payloader to the element */
  stream->srcpad = gst_ghost_pad_new (name, pad);
  gst_pad_set_active (stream->srcpad, TRUE);
  gst_element_add_pad (media->element, stream->srcpad);
  g_free (name);

  /* add stream now */
  g_array_append_val (media->streams, stream);

  setup_stream (stream, i, media);

  for (i = 0; i < 2; i++) {
    gst_element_set_state (stream->udpsink[i], GST_STATE_PAUSED);
    gst_element_set_state (stream->appsink[i], GST_STATE_PAUSED);
    gst_element_set_state (stream->tee[i], GST_STATE_PAUSED);
    gst_element_set_state (stream->selector[i], GST_STATE_PAUSED);
    gst_element_set_state (stream->appsrc[i], GST_STATE_PAUSED);
  }
  media->adding = FALSE;
}

static void
no_more_pads_cb (GstElement * element, GstRTSPMedia * media)
{
  GST_INFO ("no more pads");
  if (media->fakesink) {
    gst_object_ref (media->fakesink);
    gst_bin_remove (GST_BIN (media->pipeline), media->fakesink);
    gst_element_set_state (media->fakesink, GST_STATE_NULL);
    gst_object_unref (media->fakesink);
    media->fakesink = NULL;
    GST_INFO ("removed fakesink");
  }
}

/**
 * gst_rtsp_media_prepare:
 * @media: a #GstRTSPMedia
 *
 * Prepare @media for streaming. This function will create the pipeline and
 * other objects to manage the streaming.
 *
 * It will preroll the pipeline and collect vital information about the streams
 * such as the duration.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_prepare (GstRTSPMedia * media)
{
  GstStateChangeReturn ret;
  GstRTSPMediaStatus status;
  guint i, n_streams;
  GstRTSPMediaClass *klass;
  GstBus *bus;
  GList *walk;

  if (media->status == GST_RTSP_MEDIA_STATUS_PREPARED)
    goto was_prepared;

  if (!media->reusable && media->reused)
    goto is_reused;

  GST_INFO ("preparing media %p", media);

  /* reset some variables */
  media->is_live = FALSE;
  media->buffering = FALSE;
  /* we're preparing now */
  media->status = GST_RTSP_MEDIA_STATUS_PREPARING;

  bus = gst_pipeline_get_bus (GST_PIPELINE_CAST (media->pipeline));

  /* add the pipeline bus to our custom mainloop */
  media->source = gst_bus_create_watch (bus);
  gst_object_unref (bus);

  g_source_set_callback (media->source, (GSourceFunc) bus_message, media, NULL);

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  media->id = g_source_attach (media->source, klass->context);

  media->rtpbin = gst_element_factory_make ("gstrtpbin", NULL);

  /* add stuff to the bin */
  gst_bin_add (GST_BIN (media->pipeline), media->rtpbin);

  /* link streams we already have, other streams might appear when we have
   * dynamic elements */
  n_streams = gst_rtsp_media_n_streams (media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;

    stream = gst_rtsp_media_get_stream (media, i);

    setup_stream (stream, i, media);
  }

  for (walk = media->dynamic; walk; walk = g_list_next (walk)) {
    GstElement *elem = walk->data;

    GST_INFO ("adding callbacks for dynamic element %p", elem);

    g_signal_connect (elem, "pad-added", (GCallback) pad_added_cb, media);
    g_signal_connect (elem, "no-more-pads", (GCallback) no_more_pads_cb, media);

    /* we add a fakesink here in order to make the state change async. We remove
     * the fakesink again in the no-more-pads callback. */
    media->fakesink = gst_element_factory_make ("fakesink", "fakesink");
    gst_bin_add (GST_BIN (media->pipeline), media->fakesink);
  }

  GST_INFO ("setting pipeline to PAUSED for media %p", media);
  /* first go to PAUSED */
  ret = gst_element_set_state (media->pipeline, GST_STATE_PAUSED);
  media->target_state = GST_STATE_PAUSED;

  switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
      GST_INFO ("SUCCESS state change for media %p", media);
      break;
    case GST_STATE_CHANGE_ASYNC:
      GST_INFO ("ASYNC state change for media %p", media);
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      /* we need to go to PLAYING */
      GST_INFO ("NO_PREROLL state change: live media %p", media);
      media->is_live = TRUE;
      ret = gst_element_set_state (media->pipeline, GST_STATE_PLAYING);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto state_failed;
      break;
    case GST_STATE_CHANGE_FAILURE:
      goto state_failed;
  }

  /* now wait for all pads to be prerolled */
  status = gst_rtsp_media_get_status (media);
  if (status == GST_RTSP_MEDIA_STATUS_ERROR)
    goto state_failed;

  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_PREPARED], 0, NULL);

  GST_INFO ("object %p is prerolled", media);

  return TRUE;

  /* OK */
was_prepared:
  {
    return TRUE;
  }
  /* ERRORS */
is_reused:
  {
    GST_WARNING ("can not reuse media %p", media);
    return FALSE;
  }
state_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    unlock_streams (media);
    gst_element_set_state (media->pipeline, GST_STATE_NULL);
    gst_rtsp_media_unprepare (media);
    return FALSE;
  }
}

/**
 * gst_rtsp_media_unprepare:
 * @media: a #GstRTSPMedia
 *
 * Unprepare @media. After this call, the media should be prepared again before
 * it can be used again. If the media is set to be non-reusable, a new instance
 * must be created.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_unprepare (GstRTSPMedia * media)
{
  GstRTSPMediaClass *klass;
  gboolean success;

  if (media->status == GST_RTSP_MEDIA_STATUS_UNPREPARED)
    return TRUE;

  GST_INFO ("unprepare media %p", media);
  media->target_state = GST_STATE_NULL;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  if (klass->unprepare)
    success = klass->unprepare (media);
  else
    success = TRUE;

  media->status = GST_RTSP_MEDIA_STATUS_UNPREPARED;
  media->reused = TRUE;

  /* when the media is not reusable, this will effectively unref the media and
   * recreate it */
  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_UNPREPARED], 0, NULL);

  return success;
}

static gboolean
default_unprepare (GstRTSPMedia * media)
{
  if (media->eos_shutdown) {
    GST_DEBUG ("sending EOS for shutdown");
    /* ref so that we don't disappear */
    g_object_ref (media);
    media->eos_pending = TRUE;
    gst_element_send_event (media->pipeline, gst_event_new_eos ());
    /* we need to go to playing again for the EOS to propagate, normally in this
     * state, nothing is receiving data from us anymore so this is ok. */
    gst_element_set_state (media->pipeline, GST_STATE_PLAYING);
  } else {
    GST_DEBUG ("shutting down");
    gst_element_set_state (media->pipeline, GST_STATE_NULL);
  }
  return TRUE;
}

static void
add_udp_destination (GstRTSPMedia * media, GstRTSPMediaStream * stream,
    gchar * dest, gint min, gint max)
{
  GST_INFO ("adding %s:%d-%d", dest, min, max);
  g_signal_emit_by_name (stream->udpsink[0], "add", dest, min, NULL);
  g_signal_emit_by_name (stream->udpsink[1], "add", dest, max, NULL);
}

static void
remove_udp_destination (GstRTSPMedia * media, GstRTSPMediaStream * stream,
    gchar * dest, gint min, gint max)
{
  GST_INFO ("removing %s:%d-%d", dest, min, max);
  g_signal_emit_by_name (stream->udpsink[0], "remove", dest, min, NULL);
  g_signal_emit_by_name (stream->udpsink[1], "remove", dest, max, NULL);
}

/**
 * gst_rtsp_media_set_state:
 * @media: a #GstRTSPMedia
 * @state: the target state of the media
 * @transports: a #GArray of #GstRTSPMediaTrans pointers
 *
 * Set the state of @media to @state and for the transports in @transports.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_set_state (GstRTSPMedia * media, GstState state,
    GArray * transports)
{
  gint i;
  gboolean add, remove, do_state;
  gint old_active;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (transports != NULL, FALSE);

  /* NULL and READY are the same */
  if (state == GST_STATE_READY)
    state = GST_STATE_NULL;

  add = remove = FALSE;

  GST_INFO ("going to state %s media %p", gst_element_state_get_name (state),
      media);

  switch (state) {
    case GST_STATE_NULL:
      /* unlock the streams so that they follow the state changes from now on */
      unlock_streams (media);
      /* fallthrough */
    case GST_STATE_PAUSED:
      /* we're going from PLAYING to PAUSED, READY or NULL, remove */
      if (media->target_state == GST_STATE_PLAYING)
        remove = TRUE;
      break;
    case GST_STATE_PLAYING:
      /* we're going to PLAYING, add */
      add = TRUE;
      break;
    default:
      break;
  }
  old_active = media->active;

  for (i = 0; i < transports->len; i++) {
    GstRTSPMediaTrans *tr;
    GstRTSPMediaStream *stream;
    GstRTSPTransport *trans;

    /* we need a non-NULL entry in the array */
    tr = g_array_index (transports, GstRTSPMediaTrans *, i);
    if (tr == NULL)
      continue;

    /* we need a transport */
    if (!(trans = tr->transport))
      continue;

    /* get the stream and add the destinations */
    stream = gst_rtsp_media_get_stream (media, tr->idx);
    switch (trans->lower_transport) {
      case GST_RTSP_LOWER_TRANS_UDP:
      case GST_RTSP_LOWER_TRANS_UDP_MCAST:
      {
        gchar *dest;
        gint min, max;

        dest = trans->destination;
        if (trans->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
          min = trans->port.min;
          max = trans->port.max;
        } else {
          min = trans->client_port.min;
          max = trans->client_port.max;
        }

        if (add && !tr->active) {
          add_udp_destination (media, stream, dest, min, max);
          stream->transports = g_list_prepend (stream->transports, tr);
          tr->active = TRUE;
          media->active++;
        } else if (remove && tr->active) {
          remove_udp_destination (media, stream, dest, min, max);
          stream->transports = g_list_remove (stream->transports, tr);
          tr->active = FALSE;
          media->active--;
        }
        break;
      }
      case GST_RTSP_LOWER_TRANS_TCP:
        if (add && !tr->active) {
          GST_INFO ("adding TCP %s", trans->destination);
          stream->transports = g_list_prepend (stream->transports, tr);
          tr->active = TRUE;
          media->active++;
        } else if (remove && tr->active) {
          GST_INFO ("removing TCP %s", trans->destination);
          stream->transports = g_list_remove (stream->transports, tr);
          tr->active = FALSE;
          media->active--;
        }
        break;
      default:
        GST_INFO ("Unknown transport %d", trans->lower_transport);
        break;
    }
  }

  /* we just added the first media, do the playing state change */
  if (old_active == 0 && add)
    do_state = TRUE;
  /* if we have no more active media, do the downward state changes */
  else if (media->active == 0)
    do_state = TRUE;
  else
    do_state = FALSE;

  GST_INFO ("state %d active %d media %p do_state %d", state, media->active,
      media, do_state);

  if (media->target_state != state) {
    if (do_state) {
      if (state == GST_STATE_NULL) {
        gst_rtsp_media_unprepare (media);
      } else {
        GST_INFO ("state %s media %p", gst_element_state_get_name (state),
            media);
        media->target_state = state;
        gst_element_set_state (media->pipeline, state);
      }
    }
    g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_NEW_STATE], 0, state,
        NULL);
  }

  /* remember where we are */
  if (state == GST_STATE_PAUSED || old_active != media->active)
    collect_media_stats (media);

  return TRUE;
}

/**
 * gst_rtsp_media_remove_elements:
 * @media: a #GstRTSPMedia
 *
 * Remove all elements and the pipeline controlled by @media.
 */
void
gst_rtsp_media_remove_elements (GstRTSPMedia * media)
{
  gint i, j;

  unlock_streams (media);

  for (i = 0; i < media->streams->len; i++) {
    GstRTSPMediaStream *stream;

    GST_INFO ("Removing elements of stream %d from pipeline", i);

    stream = g_array_index (media->streams, GstRTSPMediaStream *, i);

    gst_pad_unlink (stream->srcpad, stream->send_rtp_sink);

    g_signal_handler_disconnect (stream->send_rtp_sink, stream->caps_sig);

    for (j = 0; j < 2; j++) {
      gst_element_set_state (stream->udpsrc[j], GST_STATE_NULL);
      gst_element_set_state (stream->udpsink[j], GST_STATE_NULL);
      gst_element_set_state (stream->appsrc[j], GST_STATE_NULL);
      gst_element_set_state (stream->appsink[j], GST_STATE_NULL);
      gst_element_set_state (stream->tee[j], GST_STATE_NULL);
      gst_element_set_state (stream->selector[j], GST_STATE_NULL);

      gst_bin_remove (GST_BIN (media->pipeline), stream->udpsrc[j]);
      gst_bin_remove (GST_BIN (media->pipeline), stream->udpsink[j]);
      gst_bin_remove (GST_BIN (media->pipeline), stream->appsrc[j]);
      gst_bin_remove (GST_BIN (media->pipeline), stream->appsink[j]);
      gst_bin_remove (GST_BIN (media->pipeline), stream->tee[j]);
      gst_bin_remove (GST_BIN (media->pipeline), stream->selector[j]);
    }
    if (stream->caps)
      gst_caps_unref (stream->caps);
    stream->caps = NULL;
    gst_rtsp_media_stream_free (stream);
  }
  g_array_remove_range (media->streams, 0, media->streams->len);

  gst_element_set_state (media->rtpbin, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (media->pipeline), media->rtpbin);

  gst_object_unref (media->pipeline);
  media->pipeline = NULL;
}
