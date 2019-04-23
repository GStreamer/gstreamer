/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2015 Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
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
 * SECTION:rtsp-session-media
 * @short_description: Media managed in a session
 * @see_also: #GstRTSPMedia, #GstRTSPSession
 *
 * The #GstRTSPSessionMedia object manages a #GstRTSPMedia with a given path.
 *
 * With gst_rtsp_session_media_get_transport() and
 * gst_rtsp_session_media_set_transport() the transports of a #GstRTSPStream of
 * the managed #GstRTSPMedia can be retrieved and configured.
 *
 * Use gst_rtsp_session_media_set_state() to control the media state and
 * transports.
 *
 * Last reviewed on 2013-07-16 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-session.h"

struct _GstRTSPSessionMediaPrivate
{
  GMutex lock;
  gchar *path;                  /* unmutable */
  gint path_len;                /* unmutable */
  GstRTSPMedia *media;          /* unmutable */
  GstRTSPState state;           /* protected by lock */
  guint counter;                /* protected by lock */

  GPtrArray *transports;        /* protected by lock */
};

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_session_media_debug);
#define GST_CAT_DEFAULT rtsp_session_media_debug

static void gst_rtsp_session_media_finalize (GObject * obj);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPSessionMedia, gst_rtsp_session_media,
    G_TYPE_OBJECT);

static void
gst_rtsp_session_media_class_init (GstRTSPSessionMediaClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_session_media_finalize;

  GST_DEBUG_CATEGORY_INIT (rtsp_session_media_debug, "rtspsessionmedia", 0,
      "GstRTSPSessionMedia");
}

static void
gst_rtsp_session_media_init (GstRTSPSessionMedia * media)
{
  GstRTSPSessionMediaPrivate *priv;

  media->priv = priv = gst_rtsp_session_media_get_instance_private (media);

  g_mutex_init (&priv->lock);
  priv->state = GST_RTSP_STATE_INIT;
}

static void
gst_rtsp_session_media_finalize (GObject * obj)
{
  GstRTSPSessionMedia *media;
  GstRTSPSessionMediaPrivate *priv;

  media = GST_RTSP_SESSION_MEDIA (obj);
  priv = media->priv;

  GST_INFO ("free session media %p", media);

  gst_rtsp_session_media_set_state (media, GST_STATE_NULL);

  gst_rtsp_media_unprepare (priv->media);

  g_ptr_array_unref (priv->transports);

  g_free (priv->path);
  g_object_unref (priv->media);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_session_media_parent_class)->finalize (obj);
}

static void
free_session_media (gpointer data)
{
  if (data)
    g_object_unref (data);
}

/**
 * gst_rtsp_session_media_new:
 * @path: the path
 * @media: (transfer full): the #GstRTSPMedia
 *
 * Create a new #GstRTSPSessionMedia that manages the streams
 * in @media for @path. @media should be prepared.
 *
 * Ownership is taken of @media.
 *
 * Returns: (transfer full): a new #GstRTSPSessionMedia.
 */
GstRTSPSessionMedia *
gst_rtsp_session_media_new (const gchar * path, GstRTSPMedia * media)
{
  GstRTSPSessionMediaPrivate *priv;
  GstRTSPSessionMedia *result;
  guint n_streams;
  GstRTSPMediaStatus status;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  status = gst_rtsp_media_get_status (media);
  g_return_val_if_fail (status == GST_RTSP_MEDIA_STATUS_PREPARED || status ==
      GST_RTSP_MEDIA_STATUS_SUSPENDED, NULL);

  result = g_object_new (GST_TYPE_RTSP_SESSION_MEDIA, NULL);
  priv = result->priv;

  priv->path = g_strdup (path);
  priv->path_len = strlen (path);
  priv->media = media;

  /* prealloc the streams now, filled with NULL */
  n_streams = gst_rtsp_media_n_streams (media);
  priv->transports = g_ptr_array_new_full (n_streams, free_session_media);
  g_ptr_array_set_size (priv->transports, n_streams);

  return result;
}

/**
 * gst_rtsp_session_media_matches:
 * @media: a #GstRTSPSessionMedia
 * @path: a path
 * @matched: (out): the amount of matched characters of @path
 *
 * Check if the path of @media matches @path. It @path matches, the amount of
 * matched characters is returned in @matched.
 *
 * Returns: %TRUE when @path matches the path of @media.
 */
gboolean
gst_rtsp_session_media_matches (GstRTSPSessionMedia * media,
    const gchar * path, gint * matched)
{
  GstRTSPSessionMediaPrivate *priv;
  gint len;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (matched != NULL, FALSE);

  priv = media->priv;
  len = strlen (path);

  /* path needs to be smaller than the media path */
  if (len < priv->path_len)
    return FALSE;

  /* if media path is larger, it there should be a / following the path */
  if (len > priv->path_len && path[priv->path_len] != '/')
    return FALSE;

  *matched = priv->path_len;

  return strncmp (path, priv->path, priv->path_len) == 0;
}

/**
 * gst_rtsp_session_media_get_media:
 * @media: a #GstRTSPSessionMedia
 *
 * Get the #GstRTSPMedia that was used when constructing @media
 *
 * Returns: (transfer none) (nullable): the #GstRTSPMedia of @media.
 * Remains valid as long as @media is valid.
 */
GstRTSPMedia *
gst_rtsp_session_media_get_media (GstRTSPSessionMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);

  return media->priv->media;
}

/**
 * gst_rtsp_session_media_get_base_time:
 * @media: a #GstRTSPSessionMedia
 *
 * Get the base_time of the #GstRTSPMedia in @media
 *
 * Returns: the base_time of the media.
 */
GstClockTime
gst_rtsp_session_media_get_base_time (GstRTSPSessionMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), GST_CLOCK_TIME_NONE);

  return gst_rtsp_media_get_base_time (media->priv->media);
}

/**
 * gst_rtsp_session_media_get_rtpinfo:
 * @media: a #GstRTSPSessionMedia
 *
 * Retrieve the RTP-Info header string for all streams in @media
 * with configured transports.
 *
 * Returns: (transfer full) (nullable): The RTP-Info as a string or
 * %NULL when no RTP-Info could be generated, g_free() after usage.
 */
gchar *
gst_rtsp_session_media_get_rtpinfo (GstRTSPSessionMedia * media)
{
  GstRTSPSessionMediaPrivate *priv;
  GString *rtpinfo = NULL;
  GstRTSPStreamTransport *transport;
  GstRTSPStream *stream;
  guint i, n_streams;
  GstClockTime earliest = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);

  priv = media->priv;
  g_mutex_lock (&priv->lock);

  if (gst_rtsp_media_get_status (priv->media) != GST_RTSP_MEDIA_STATUS_PREPARED)
    goto not_prepared;

  n_streams = priv->transports->len;

  /* first step, take lowest running-time from all streams */
  GST_LOG_OBJECT (media, "determining start time among %d transports",
      n_streams);

  for (i = 0; i < n_streams; i++) {
    GstClockTime running_time;

    transport = g_ptr_array_index (priv->transports, i);
    if (transport == NULL) {
      GST_DEBUG_OBJECT (media, "ignoring unconfigured transport %d", i);
      continue;
    }

    stream = gst_rtsp_stream_transport_get_stream (transport);
    if (!gst_rtsp_stream_is_sender (stream))
      continue;
    if (!gst_rtsp_stream_get_rtpinfo (stream, NULL, NULL, NULL, &running_time))
      continue;

    GST_LOG_OBJECT (media, "running time of %d stream: %" GST_TIME_FORMAT, i,
        GST_TIME_ARGS (running_time));

    if (!GST_CLOCK_TIME_IS_VALID (earliest)) {
      earliest = running_time;
    } else {
      earliest = MIN (earliest, running_time);
    }
  }

  GST_LOG_OBJECT (media, "media start time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (earliest));

  /* next step, scale all rtptime of all streams to lowest running-time */
  GST_LOG_OBJECT (media, "collecting RTP info for %d transports", n_streams);

  for (i = 0; i < n_streams; i++) {
    gchar *stream_rtpinfo;

    transport = g_ptr_array_index (priv->transports, i);
    if (transport == NULL) {
      GST_DEBUG_OBJECT (media, "ignoring unconfigured transport %d", i);
      continue;
    }

    stream_rtpinfo =
        gst_rtsp_stream_transport_get_rtpinfo (transport, earliest);
    if (stream_rtpinfo == NULL) {
      GST_DEBUG_OBJECT (media, "ignoring unknown RTPInfo %d", i);
      continue;
    }

    if (rtpinfo == NULL)
      rtpinfo = g_string_new ("");
    else
      g_string_append (rtpinfo, ", ");

    g_string_append (rtpinfo, stream_rtpinfo);
    g_free (stream_rtpinfo);
  }
  g_mutex_unlock (&priv->lock);

  if (rtpinfo == NULL) {
    GST_WARNING_OBJECT (media, "RTP info is empty");
    return NULL;
  }
  return g_string_free (rtpinfo, FALSE);

  /* ERRORS */
not_prepared:
  {
    g_mutex_unlock (&priv->lock);
    GST_ERROR_OBJECT (media, "media was not prepared");
    return NULL;
  }
}

/**
 * gst_rtsp_session_media_set_transport:
 * @media: a #GstRTSPSessionMedia
 * @stream: a #GstRTSPStream
 * @tr: (transfer full): a #GstRTSPTransport
 *
 * Configure the transport for @stream to @tr in @media.
 *
 * Returns: (transfer none): the new or updated #GstRTSPStreamTransport for @stream.
 */
GstRTSPStreamTransport *
gst_rtsp_session_media_set_transport (GstRTSPSessionMedia * media,
    GstRTSPStream * stream, GstRTSPTransport * tr)
{
  GstRTSPSessionMediaPrivate *priv;
  GstRTSPStreamTransport *result;
  guint idx;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (tr != NULL, NULL);
  priv = media->priv;
  idx = gst_rtsp_stream_get_index (stream);
  g_return_val_if_fail (idx < priv->transports->len, NULL);

  g_mutex_lock (&priv->lock);
  result = g_ptr_array_index (priv->transports, idx);
  if (result == NULL) {
    result = gst_rtsp_stream_transport_new (stream, tr);
    g_ptr_array_index (priv->transports, idx) = result;
    g_mutex_unlock (&priv->lock);
  } else {
    gst_rtsp_stream_transport_set_transport (result, tr);
    g_mutex_unlock (&priv->lock);
  }

  return result;
}

/**
 * gst_rtsp_session_media_get_transport:
 * @media: a #GstRTSPSessionMedia
 * @idx: the stream index
 *
 * Get a previously created #GstRTSPStreamTransport for the stream at @idx.
 *
 * Returns: (transfer none) (nullable): a #GstRTSPStreamTransport that is
 * valid until the session of @media is unreffed.
 */
GstRTSPStreamTransport *
gst_rtsp_session_media_get_transport (GstRTSPSessionMedia * media, guint idx)
{
  GstRTSPSessionMediaPrivate *priv;
  GstRTSPStreamTransport *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);
  priv = media->priv;
  g_return_val_if_fail (idx < priv->transports->len, NULL);

  g_mutex_lock (&priv->lock);
  result = g_ptr_array_index (priv->transports, idx);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_session_media_get_transports:
 * @media: a #GstRTSPSessionMedia
 *
 * Get a list of all available #GstRTSPStreamTransport in this session.
 *
 * Returns: (transfer full) (element-type GstRTSPStreamTransport): a
 * list of #GstRTSPStreamTransport, g_ptr_array_unref () after usage.
 *
 * Since: 1.14
 */
GPtrArray *
gst_rtsp_session_media_get_transports (GstRTSPSessionMedia * media)
{
  GstRTSPSessionMediaPrivate *priv;
  GPtrArray *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);
  priv = media->priv;

  g_mutex_lock (&priv->lock);
  result = g_ptr_array_ref (priv->transports);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_session_media_alloc_channels:
 * @media: a #GstRTSPSessionMedia
 * @range: (out): a #GstRTSPRange
 *
 * Fill @range with the next available min and max channels for
 * interleaved transport.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_session_media_alloc_channels (GstRTSPSessionMedia * media,
    GstRTSPRange * range)
{
  GstRTSPSessionMediaPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  range->min = priv->counter++;
  range->max = priv->counter++;
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_session_media_set_state:
 * @media: a #GstRTSPSessionMedia
 * @state: the new state
 *
 * Tell the media object @media to change to @state.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_session_media_set_state (GstRTSPSessionMedia * media, GstState state)
{
  GstRTSPSessionMediaPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  ret = gst_rtsp_media_set_state (priv->media, state, priv->transports);
  g_mutex_unlock (&priv->lock);

  return ret;
}

/**
 * gst_rtsp_session_media_set_rtsp_state:
 * @media: a #GstRTSPSessionMedia
 * @state: a #GstRTSPState
 *
 * Set the RTSP state of @media to @state.
 */
void
gst_rtsp_session_media_set_rtsp_state (GstRTSPSessionMedia * media,
    GstRTSPState state)
{
  GstRTSPSessionMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SESSION_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->state = state;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_session_media_get_rtsp_state:
 * @media: a #GstRTSPSessionMedia
 *
 * Get the current RTSP state of @media.
 *
 * Returns: the current RTSP state of @media.
 */
GstRTSPState
gst_rtsp_session_media_get_rtsp_state (GstRTSPSessionMedia * media)
{
  GstRTSPSessionMediaPrivate *priv;
  GstRTSPState ret;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media),
      GST_RTSP_STATE_INVALID);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  ret = priv->state;
  g_mutex_unlock (&priv->lock);

  return ret;
}
