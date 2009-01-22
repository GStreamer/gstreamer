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

#include "rtsp-session.h"

#undef DEBUG

static void gst_rtsp_session_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPSession, gst_rtsp_session, G_TYPE_OBJECT);

static void
gst_rtsp_session_class_init (GstRTSPSessionClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_session_finalize;
}

static void
gst_rtsp_session_init (GstRTSPSession * session)
{
}

static void
gst_rtsp_session_free_stream (GstRTSPSessionStream *stream)
{
  if (stream->client_trans)
    gst_rtsp_transport_free (stream->client_trans);
  g_free (stream->destination);
  if (stream->server_trans)
    gst_rtsp_transport_free (stream->server_trans);

  if (stream->udpsrc[0])
    gst_object_unref (stream->udpsrc[0]);

  g_free (stream);
}

static void
gst_rtsp_session_free_media (GstRTSPSessionMedia *media)
{
  GList *walk;

  gst_element_set_state (media->pipeline, GST_STATE_NULL);

  if (media->factory)
    g_object_unref (media->factory);

  for (walk = media->streams; walk; walk = g_list_next (walk)) {
    GstRTSPSessionStream *stream = (GstRTSPSessionStream *) walk->data; 

    gst_rtsp_session_free_stream (stream);
  }
  if (media->pipeline)
    gst_object_unref (media->pipeline);
  g_list_free (media->streams);
}

static void
gst_rtsp_session_finalize (GObject * obj)
{
  GstRTSPSession *session;
  GList *walk;

  session = GST_RTSP_SESSION (obj);

  g_free (session->sessionid);

  for (walk = session->medias; walk; walk = g_list_next (walk)) {
    GstRTSPSessionMedia *media = (GstRTSPSessionMedia *) walk->data; 

    gst_rtsp_session_free_media (media);
  }
  g_list_free (session->medias);

  G_OBJECT_CLASS (gst_rtsp_session_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_session_get_media:
 * @sess: a #GstRTSPSession
 * @location: the url for the media
 * @factory: a #GstRTSPMediaFactory
 *
 * Get or create the session information for @factory.
 *
 * Returns: the configuration for @factory in @sess.
 */
GstRTSPSessionMedia *
gst_rtsp_session_get_media (GstRTSPSession *sess, const gchar *location, GstRTSPMediaFactory *factory)
{
  GstRTSPSessionMedia *result;
  GList *walk;

  result = NULL;

  for (walk = sess->medias; walk; walk = g_list_next (walk)) {
    result = (GstRTSPSessionMedia *) walk->data; 

    if (result->factory == factory)
      break;

    result = NULL;
  }
  if (result == NULL) {
    result = g_new0 (GstRTSPSessionMedia, 1);
    result->factory = factory;
    result->pipeline = gst_pipeline_new ("pipeline");

    /* construct media and add to the pipeline */
    result->mediabin = gst_rtsp_media_factory_construct (factory, location);
    if (result->mediabin == NULL)
      goto no_media;
    
    gst_bin_add (GST_BIN_CAST (result->pipeline), result->mediabin->element);

    result->rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

    /* add stuf to the bin */
    gst_bin_add (GST_BIN (result->pipeline), result->rtpbin);

    gst_element_set_state (result->pipeline, GST_STATE_READY);

    sess->medias = g_list_prepend (sess->medias, result);
  }
  return result;

  /* ERRORS */
no_media:
  {
    gst_rtsp_session_free_media (result);
    return NULL;
  }
}

/**
 * gst_rtsp_session_media_get_stream:
 * @media: a #GstRTSPSessionMedia
 * @idx: the stream index
 *
 * Get a previously created or create a new #GstRTSPSessionStream at @idx.
 *
 * Returns: a #GstRTSPSessionStream that is valid until the session of @media
 * is unreffed.
 */
GstRTSPSessionStream *
gst_rtsp_session_media_get_stream (GstRTSPSessionMedia *media, guint idx)
{
  GstRTSPSessionStream *result;
  GList *walk;

  result = NULL;

  for (walk = media->streams; walk; walk = g_list_next (walk)) {
    result = (GstRTSPSessionStream *) walk->data; 

    if (result->idx == idx)
      break;

    result = NULL;
  }
  if (result == NULL) {
    result = g_new0 (GstRTSPSessionStream, 1);
    result->idx = idx;
    result->media = media;
    result->media_stream = gst_rtsp_media_bin_get_stream (media->mediabin, idx);

    media->streams = g_list_prepend (media->streams, result);
  }
  return result;
}

/**
 * gst_rtsp_session_new:
 *
 * Create a new #GstRTSPSession instance.
 */
GstRTSPSession *
gst_rtsp_session_new (const gchar *sessionid)
{
  GstRTSPSession *result;

  result = g_object_new (GST_TYPE_RTSP_SESSION, NULL);
  result->sessionid = g_strdup (sessionid);

  return result;
}

static gboolean
alloc_udp_ports (GstRTSPSessionStream * stream)
{
  GstStateChangeReturn ret;
  GstElement *udpsrc0, *udpsrc1;
  GstElement *udpsink0, *udpsink1;
  gint tmp_rtp, tmp_rtcp;
  guint count;
  gint rtpport, rtcpport, sockfd;
  gchar *name;

  udpsrc0 = NULL;
  udpsrc1 = NULL;
  udpsink0 = NULL;
  udpsink1 = NULL;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  udpsrc0 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
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
  udpsrc1 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
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

  name = g_strdup_printf ("udp://%s:%d", stream->destination, stream->client_trans->client_port.min);
  udpsink0 = gst_element_make_from_uri (GST_URI_SINK, name, NULL);
  g_free (name);

  if (!udpsink0)
    goto no_udp_protocol;

  g_object_get (G_OBJECT (udpsrc0), "sock", &sockfd, NULL);
  g_object_set (G_OBJECT (udpsink0), "sockfd", sockfd, NULL);
  g_object_set (G_OBJECT (udpsink0), "closefd", FALSE, NULL);

  name = g_strdup_printf ("udp://%s:%d", stream->destination, stream->client_trans->client_port.max);
  udpsink1 = gst_element_make_from_uri (GST_URI_SINK, name, NULL);
  g_free (name);

  if (!udpsink1)
    goto no_udp_protocol;

  g_object_get (G_OBJECT (udpsrc1), "sock", &sockfd, NULL);
  g_object_set (G_OBJECT (udpsink1), "sockfd", sockfd, NULL);
  g_object_set (G_OBJECT (udpsink1), "closefd", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "async", FALSE, NULL);


  /* we keep these elements, we configure all in configure_transport when the
   * server told us to really use the UDP ports. */
  stream->udpsrc[0] = gst_object_ref (udpsrc0);
  stream->udpsrc[1] = gst_object_ref (udpsrc1);
  stream->udpsink[0] = gst_object_ref (udpsink0);
  stream->udpsink[1] = gst_object_ref (udpsink1);
  stream->server_trans->server_port.min = rtpport;
  stream->server_trans->server_port.max = rtcpport;

  /* they are ours now */
  gst_object_sink (udpsrc0);
  gst_object_sink (udpsrc1);
  gst_object_sink (udpsink0);
  gst_object_sink (udpsink1);

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


/**
 * gst_rtsp_session_stream_init_udp:
 * @stream: a #GstRTSPSessionStream
 * @ct: a client #GstRTSPTransport
 *
 * Set @ct as the client transport and create and return a matching server
 * transport. After this call the needed ports and elements will be created and
 * initialized.
 * 
 * Returns: a server transport or NULL if something went wrong.
 */
GstRTSPTransport *
gst_rtsp_session_stream_set_transport (GstRTSPSessionStream *stream, 
    const gchar *destination, GstRTSPTransport *ct)
{
  GstRTSPTransport *st;
  GstPad *pad;
  gchar *name;
  GstRTSPSessionMedia *media;

  media = stream->media;

  /* prepare the server transport */
  gst_rtsp_transport_new (&st);

  st->trans = ct->trans;
  st->profile = ct->profile;
  st->lower_transport = ct->lower_transport;
  st->client_port = ct->client_port;

  /* keep track of the transports */
  g_free (stream->destination);
  stream->destination = g_strdup (destination);
  if (stream->client_trans)
    gst_rtsp_transport_free (stream->client_trans);
  stream->client_trans = ct;
  if (stream->server_trans)
    gst_rtsp_transport_free (stream->server_trans);
  stream->server_trans = st;

  alloc_udp_ports (stream);

  gst_bin_add (GST_BIN (media->pipeline), stream->udpsink[0]);
  gst_bin_add (GST_BIN (media->pipeline), stream->udpsink[1]);
  gst_bin_add (GST_BIN (media->pipeline), stream->udpsrc[1]);

  /* hook up the stream to the RTP session elements. */
  name = g_strdup_printf ("send_rtp_sink_%d", stream->idx);
  stream->send_rtp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtp_src_%d", stream->idx);
  stream->send_rtp_src = gst_element_get_static_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtcp_src_%d", stream->idx);
  stream->send_rtcp_src = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtcp_sink_%d", stream->idx);
  stream->recv_rtcp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);

  gst_pad_link (stream->media_stream->srcpad, stream->send_rtp_sink);
  pad = gst_element_get_static_pad (stream->udpsink[0], "sink");
  gst_pad_link (stream->send_rtp_src, pad);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (stream->udpsink[1], "sink");
  gst_pad_link (stream->send_rtcp_src, pad);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (stream->udpsrc[1], "src");
  gst_pad_link (pad, stream->recv_rtcp_sink);
  gst_object_unref (pad);

  return st;
}

/**
 * gst_rtsp_session_media_play:
 * @media: a #GstRTSPSessionMedia
 *
 * Tell the media object @media to start playing and streaming to the client.
 *
 * Returns: a #GstStateChangeReturn
 */
GstStateChangeReturn
gst_rtsp_session_media_play (GstRTSPSessionMedia *media)
{
  GstStateChangeReturn ret;

  ret = gst_element_set_state (media->pipeline, GST_STATE_PLAYING);

  return ret;
}

/**
 * gst_rtsp_session_media_pause:
 * @media: a #GstRTSPSessionMedia
 *
 * Tell the media object @media to pause.
 *
 * Returns: a #GstStateChangeReturn
 */
GstStateChangeReturn
gst_rtsp_session_media_pause (GstRTSPSessionMedia *media)
{
  GstStateChangeReturn ret;

  ret = gst_element_set_state (media->pipeline, GST_STATE_PAUSED);

  return ret;
}

/**
 * gst_rtsp_session_media_stop:
 * @media: a #GstRTSPSessionMedia
 *
 * Tell the media object @media to stop playing. After this call the media
 * cannot be played or paused anymore
 *
 * Returns: a #GstStateChangeReturn
 */
GstStateChangeReturn
gst_rtsp_session_media_stop (GstRTSPSessionMedia *media)
{
  GstStateChangeReturn ret;

  ret = gst_element_set_state (media->pipeline, GST_STATE_NULL);

  return ret;
}


