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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include <string.h>

#include "rtsp-session.h"

#undef DEBUG

#define DEFAULT_TIMEOUT	60

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_session_media_debug);
#define GST_CAT_DEFAULT rtsp_session_media_debug

static void gst_rtsp_session_media_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPSessionMedia, gst_rtsp_session_media, G_TYPE_OBJECT);

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
  media->state = GST_RTSP_STATE_INIT;
}

static void
gst_rtsp_session_media_finalize (GObject * obj)
{
  GstRTSPSessionMedia *media;

  media = GST_RTSP_SESSION_MEDIA (obj);

  GST_INFO ("free session media %p", media);

  gst_rtsp_session_media_set_state (media, GST_STATE_NULL);

  g_ptr_array_unref (media->transports);

  gst_rtsp_url_free (media->url);
  g_object_unref (media->media);

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
 * @url: the #GstRTSPUrl
 * @media: the #GstRTSPMedia
 *
 * Create a new #GstRTPSessionMedia that manages the streams
 * in @media for @url. @media should be prepared.
 *
 * Ownership is taken of @media.
 *
 * Returns: a new #GstRTSPSessionMedia.
 */
GstRTSPSessionMedia *
gst_rtsp_session_media_new (const GstRTSPUrl * url, GstRTSPMedia * media)
{
  GstRTSPSessionMedia *result;
  guint n_streams;

  g_return_val_if_fail (url != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (media->status == GST_RTSP_MEDIA_STATUS_PREPARED, NULL);

  result = g_object_new (GST_TYPE_RTSP_SESSION_MEDIA, NULL);
  result->url = gst_rtsp_url_copy ((GstRTSPUrl *) url);
  result->media = media;

  /* prealloc the streams now, filled with NULL */
  n_streams = gst_rtsp_media_n_streams (media);
  result->transports = g_ptr_array_new_full (n_streams, free_session_media);
  g_ptr_array_set_size (result->transports, n_streams);

  return result;
}

/**
 * gst_rtsp_session_media_set_transport:
 * @media: a #GstRTSPSessionMedia
 * @stream: a #GstRTSPStream
 * @tr: a #GstRTSPTransport
 *
 * Configure the transport for @stream to @tr in @media.
 *
 * Returns: (transfer none): the new or updated #GstRTSPStreamTransport for @stream.
 */
GstRTSPStreamTransport *
gst_rtsp_session_media_set_transport (GstRTSPSessionMedia * media,
    GstRTSPStream * stream, GstRTSPTransport * tr)
{
  GstRTSPStreamTransport *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (stream->idx < media->transports->len, NULL);

  result = g_ptr_array_index (media->transports, stream->idx);
  if (result == NULL) {
    result = gst_rtsp_stream_transport_new (stream, tr);
    g_ptr_array_index (media->transports, stream->idx) = result;
  } else {
    gst_rtsp_stream_transport_set_transport (result, tr);
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
 * Returns: (transfer none): a #GstRTSPStreamTransport that is valid until the
 * session of @media is unreffed.
 */
GstRTSPStreamTransport *
gst_rtsp_session_media_get_transport (GstRTSPSessionMedia * media, guint idx)
{
  GstRTSPStreamTransport *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), NULL);
  g_return_val_if_fail (idx < media->transports->len, NULL);

  result = g_ptr_array_index (media->transports, idx);

  return result;
}

/**
 * gst_rtsp_session_media_alloc_channels:
 * @media: a #GstRTSPSessionMedia
 * @range: a #GstRTSPRange
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
  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), FALSE);

  range->min = media->counter++;
  range->max = media->counter++;

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
  gboolean ret;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_MEDIA (media), FALSE);

  ret = gst_rtsp_media_set_state (media->media, state, media->transports);

  return ret;
}
