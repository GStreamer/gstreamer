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

#include <gio/gio.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include "rtsp-stream.h"

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_stream_debug);
#define GST_CAT_DEFAULT rtsp_stream_debug

static GQuark ssrc_stream_map_key;

static void gst_rtsp_stream_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPStream, gst_rtsp_stream, G_TYPE_OBJECT);

static void
gst_rtsp_stream_class_init (GstRTSPStreamClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_stream_finalize;

  GST_DEBUG_CATEGORY_INIT (rtsp_stream_debug, "rtspstream", 0, "GstRTSPStream");

  ssrc_stream_map_key = g_quark_from_static_string ("GstRTSPServer.stream");
}

static void
gst_rtsp_stream_init (GstRTSPStream * media)
{
}

static void
gst_rtsp_stream_finalize (GObject * obj)
{
  GstRTSPStream *stream;

  stream = GST_RTSP_STREAM (obj);

  g_assert (!stream->is_joined);

  gst_object_unref (stream->payloader);
  gst_object_unref (stream->srcpad);

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

  G_OBJECT_CLASS (gst_rtsp_stream_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_stream_new:
 * @idx: an index
 * @srcpad: a #GstPad
 * @payloader: a #GstElement
 *
 * Create a new media stream with index @idx that handles RTP data on
 * @srcpad and has a payloader element @payloader.
 *
 * Returns: a new #GstRTSPStream
 */
GstRTSPStream *
gst_rtsp_stream_new (guint idx, GstElement * payloader, GstPad * srcpad)
{
  GstRTSPStream *stream;

  g_return_val_if_fail (GST_IS_ELEMENT (payloader), NULL);
  g_return_val_if_fail (GST_IS_PAD (srcpad), NULL);
  g_return_val_if_fail (GST_PAD_IS_SRC (srcpad), NULL);

  stream = g_object_new (GST_TYPE_RTSP_STREAM, NULL);
  stream->idx = idx;
  stream->payloader = gst_object_ref (payloader);
  stream->srcpad = gst_object_ref (srcpad);

  return stream;
}

/**
 * gst_rtsp_stream_set_mtu:
 * @stream: a #GstRTSPStream
 * @mtu: a new MTU
 *
 * Configure the mtu in the payloader of @stream to @mtu.
 */
void
gst_rtsp_stream_set_mtu (GstRTSPStream * stream, guint mtu)
{
  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  g_object_set (G_OBJECT (stream->payloader), "mtu", mtu, NULL);
}

/**
 * gst_rtsp_stream_get_mtu:
 * @stream: a #GstRTSPStream
 *
 * Get the configured MTU in the payloader of @stream.
 *
 * Returns: the MTU of the payloader.
 */
guint
gst_rtsp_stream_get_mtu (GstRTSPStream * stream)
{
  guint mtu;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), 0);

  g_object_get (G_OBJECT (stream->payloader), "mtu", &mtu, NULL);

  return mtu;
}

static gboolean
alloc_ports (GstRTSPStream * stream)
{
  GstStateChangeReturn ret;
  GstElement *udpsrc0, *udpsrc1;
  GstElement *udpsink0, *udpsink1;
  gint tmp_rtp, tmp_rtcp;
  guint count;
  gint rtpport, rtcpport;
  GSocket *socket;
  const gchar *host;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  udpsrc0 = NULL;
  udpsrc1 = NULL;
  udpsink0 = NULL;
  udpsink1 = NULL;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;

  if (stream->is_ipv6)
    host = "udp://[::0]";
  else
    host = "udp://0.0.0.0";

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  udpsrc0 = gst_element_make_from_uri (GST_URI_SRC, host, NULL, NULL);
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
  udpsrc1 = gst_element_make_from_uri (GST_URI_SRC, host, NULL, NULL);
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

  g_object_get (G_OBJECT (udpsrc0), "socket", &socket, NULL);
  g_object_set (G_OBJECT (udpsink0), "socket", socket, NULL);
  g_object_set (G_OBJECT (udpsink0), "close-socket", FALSE, NULL);

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
    g_object_set (G_OBJECT (udpsink0), "buffer-size", stream->buffer_size,
        NULL);
  } else {
    GST_WARNING ("multiudpsink version found without buffer-size property");
  }

  g_object_get (G_OBJECT (udpsrc1), "socket", &socket, NULL);
  g_object_set (G_OBJECT (udpsink1), "socket", socket, NULL);
  g_object_set (G_OBJECT (udpsink1), "close-socket", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "async", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink0), "auto-multicast", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink0), "loop", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "auto-multicast", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "loop", FALSE, NULL);

  /* we keep these elements, we will further configure them when the
   * client told us to really use the UDP ports. */
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
caps_notify (GstPad * pad, GParamSpec * unused, GstRTSPStream * stream)
{
  GstCaps *newcaps, *oldcaps;

  newcaps = gst_pad_get_current_caps (pad);

  oldcaps = stream->caps;
  stream->caps = newcaps;

  if (oldcaps)
    gst_caps_unref (oldcaps);

  GST_INFO ("stream %p received caps %p, %" GST_PTR_FORMAT, stream, newcaps,
      newcaps);
}

static void
dump_structure (const GstStructure * s)
{
  gchar *sstr;

  sstr = gst_structure_to_string (s);
  GST_INFO ("structure: %s", sstr);
  g_free (sstr);
}

static GstRTSPStreamTransport *
find_transport (GstRTSPStream * stream, const gchar * rtcp_from)
{
  GList *walk;
  GstRTSPStreamTransport *result = NULL;
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

  GST_INFO ("finding %s:%d in %d transports", dest, port,
      g_list_length (stream->transports));

  for (walk = stream->transports; walk; walk = g_list_next (walk)) {
    GstRTSPStreamTransport *trans = walk->data;
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

static GstRTSPStreamTransport *
check_transport (GObject * source, GstRTSPStream * stream)
{
  GstStructure *stats;
  GstRTSPStreamTransport *trans;

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
  }

  return trans;
}


static void
on_new_ssrc (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GstRTSPStreamTransport *trans;

  GST_INFO ("%p: new source %p", stream, source);

  trans = check_transport (source, stream);

  if (trans)
    GST_INFO ("%p: source %p for transport %p", stream, source, trans);
}

static void
on_ssrc_sdes (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GST_INFO ("%p: new SDES %p", stream, source);
}

static void
on_ssrc_active (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GstRTSPStreamTransport *trans;

  trans = check_transport (source, stream);

  if (trans)
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
on_bye_ssrc (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GST_INFO ("%p: source %p bye", stream, source);
}

static void
on_bye_timeout (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GstRTSPStreamTransport *trans;

  GST_INFO ("%p: source %p bye timeout", stream, source);

  if ((trans = g_object_get_qdata (source, ssrc_stream_map_key))) {
    trans->rtpsource = NULL;
    trans->timeout = TRUE;
  }
}

static void
on_timeout (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GstRTSPStreamTransport *trans;

  GST_INFO ("%p: source %p timeout", stream, source);

  if ((trans = g_object_get_qdata (source, ssrc_stream_map_key))) {
    trans->rtpsource = NULL;
    trans->timeout = TRUE;
  }
}

static GstFlowReturn
handle_new_sample (GstAppSink * sink, gpointer user_data)
{
  GList *walk;
  GstSample *sample;
  GstBuffer *buffer;
  GstRTSPStream *stream;

  sample = gst_app_sink_pull_sample (sink);
  if (!sample)
    return GST_FLOW_OK;

  stream = (GstRTSPStream *) user_data;
  buffer = gst_sample_get_buffer (sample);

  for (walk = stream->transports; walk; walk = g_list_next (walk)) {
    GstRTSPStreamTransport *tr = (GstRTSPStreamTransport *) walk->data;

    if (GST_ELEMENT_CAST (sink) == stream->appsink[0]) {
      if (tr->send_rtp)
        tr->send_rtp (buffer, tr->transport->interleaved.min, tr->user_data);
    } else {
      if (tr->send_rtcp)
        tr->send_rtcp (buffer, tr->transport->interleaved.max, tr->user_data);
    }
  }
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static GstAppSinkCallbacks sink_cb = {
  NULL,                         /* not interested in EOS */
  NULL,                         /* not interested in preroll samples */
  handle_new_sample,
};

/**
 * gst_rtsp_stream_join_bin:
 * @stream: a #GstRTSPStream
 * @bin: a #GstBin to join
 * @rtpbin: a rtpbin element in @bin
 *
 * Join the #Gstbin @bin that contains the element @rtpbin.
 *
 * @stream will link to @rtpbin, which must be inside @bin.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_stream_join_bin (GstRTSPStream * stream, GstBin * bin,
    GstElement * rtpbin)
{
  gint i, idx;
  gchar *name;
  GstPad *pad, *teepad, *queuepad, *selpad;
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (rtpbin), FALSE);

  idx = stream->idx;

  if (stream->is_joined)
    return TRUE;

  GST_INFO ("stream %p joining bin", stream);

  if (!alloc_ports (stream))
    goto no_ports;

  /* add the ports to the pipeline */
  for (i = 0; i < 2; i++) {
    gst_bin_add (bin, stream->udpsink[i]);
    gst_bin_add (bin, stream->udpsrc[i]);
  }

  /* create elements for the TCP transfer */
  for (i = 0; i < 2; i++) {
    stream->appsrc[i] = gst_element_factory_make ("appsrc", NULL);
    stream->appqueue[i] = gst_element_factory_make ("queue", NULL);
    stream->appsink[i] = gst_element_factory_make ("appsink", NULL);
    g_object_set (stream->appsink[i], "async", FALSE, "sync", FALSE, NULL);
    g_object_set (stream->appsink[i], "emit-signals", FALSE, NULL);
    gst_bin_add (bin, stream->appqueue[i]);
    gst_bin_add (bin, stream->appsink[i]);
    gst_bin_add (bin, stream->appsrc[i]);
    gst_app_sink_set_callbacks (GST_APP_SINK_CAST (stream->appsink[i]),
        &sink_cb, stream, NULL);
  }

  /* hook up the stream to the RTP session elements. */
  name = g_strdup_printf ("send_rtp_sink_%u", idx);
  stream->send_rtp_sink = gst_element_get_request_pad (rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtp_src_%u", idx);
  stream->send_rtp_src = gst_element_get_static_pad (rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtcp_src_%u", idx);
  stream->send_rtcp_src = gst_element_get_request_pad (rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtcp_sink_%u", idx);
  stream->recv_rtcp_sink = gst_element_get_request_pad (rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtp_sink_%u", idx);
  stream->recv_rtp_sink = gst_element_get_request_pad (rtpbin, name);
  g_free (name);
  /* get the session */
  g_signal_emit_by_name (rtpbin, "get-internal-session", idx, &stream->session);

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
  gst_bin_add (bin, stream->tee[0]);

  pad = gst_element_get_static_pad (stream->tee[0], "sink");
  gst_pad_link (stream->send_rtp_src, pad);
  gst_object_unref (pad);

  /* link RTP sink, we're pretty sure this will work. */
  teepad = gst_element_get_request_pad (stream->tee[0], "src_%u");
  pad = gst_element_get_static_pad (stream->udpsink[0], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  teepad = gst_element_get_request_pad (stream->tee[0], "src_%u");
  pad = gst_element_get_static_pad (stream->appqueue[0], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  queuepad = gst_element_get_static_pad (stream->appqueue[0], "src");
  pad = gst_element_get_static_pad (stream->appsink[0], "sink");
  gst_pad_link (queuepad, pad);
  gst_object_unref (pad);
  gst_object_unref (queuepad);

  /* make tee for RTCP */
  stream->tee[1] = gst_element_factory_make ("tee", NULL);
  gst_bin_add (bin, stream->tee[1]);

  pad = gst_element_get_static_pad (stream->tee[1], "sink");
  gst_pad_link (stream->send_rtcp_src, pad);
  gst_object_unref (pad);

  /* link RTCP elements */
  teepad = gst_element_get_request_pad (stream->tee[1], "src_%u");
  pad = gst_element_get_static_pad (stream->udpsink[1], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  teepad = gst_element_get_request_pad (stream->tee[1], "src_%u");
  pad = gst_element_get_static_pad (stream->appqueue[1], "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  queuepad = gst_element_get_static_pad (stream->appqueue[1], "src");
  pad = gst_element_get_static_pad (stream->appsink[1], "sink");
  gst_pad_link (queuepad, pad);
  gst_object_unref (pad);
  gst_object_unref (queuepad);

  /* make selector for the RTP receivers */
  stream->selector[0] = gst_element_factory_make ("funnel", NULL);
  gst_bin_add (bin, stream->selector[0]);

  pad = gst_element_get_static_pad (stream->selector[0], "src");
  gst_pad_link (pad, stream->recv_rtp_sink);
  gst_object_unref (pad);

  selpad = gst_element_get_request_pad (stream->selector[0], "sink_%u");
  pad = gst_element_get_static_pad (stream->udpsrc[0], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);
  selpad = gst_element_get_request_pad (stream->selector[0], "sink_%u");
  pad = gst_element_get_static_pad (stream->appsrc[0], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);

  /* make selector for the RTCP receivers */
  stream->selector[1] = gst_element_factory_make ("funnel", NULL);
  gst_bin_add (bin, stream->selector[1]);

  pad = gst_element_get_static_pad (stream->selector[1], "src");
  gst_pad_link (pad, stream->recv_rtcp_sink);
  gst_object_unref (pad);

  selpad = gst_element_get_request_pad (stream->selector[1], "sink_%u");
  pad = gst_element_get_static_pad (stream->udpsrc[1], "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);

  selpad = gst_element_get_request_pad (stream->selector[1], "sink_%u");
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

  stream->is_joined = TRUE;

  return TRUE;

  /* ERRORS */
no_ports:
  {
    GST_WARNING ("failed to allocate ports %d", idx);
    return FALSE;
  }
link_failed:
  {
    GST_WARNING ("failed to link stream %d", idx);
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_leave_bin:
 * @stream: a #GstRTSPStream
 * @bin: a #GstBin
 * @rtpbin: a rtpbin #GstElement
 *
 * Remove the elements of @stream from the bin
 *
 * Return: %TRUE on success.
 */
gboolean
gst_rtsp_stream_leave_bin (GstRTSPStream * stream, GstBin * bin,
    GstElement * rtpbin)
{
  gint i;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (rtpbin), FALSE);

  if (!stream->is_joined)
    return TRUE;

  GST_INFO ("stream %p leaving bin", stream);

  gst_pad_unlink (stream->srcpad, stream->send_rtp_sink);

  g_signal_handler_disconnect (stream->send_rtp_sink, stream->caps_sig);

  /* FIXME not entirely the opposite of join_bin */
  for (i = 0; i < 2; i++) {
    gst_bin_remove (bin, stream->udpsrc[i]);
    gst_bin_remove (bin, stream->udpsink[i]);
    gst_bin_remove (bin, stream->appsrc[i]);
    gst_bin_remove (bin, stream->appsink[i]);
    gst_bin_remove (bin, stream->appqueue[i]);
    gst_bin_remove (bin, stream->tee[i]);
    gst_bin_remove (bin, stream->selector[i]);
  }
  stream->is_joined = FALSE;

  return TRUE;
}

/**
 * gst_rtsp_stream_get_rtpinfo:
 * @stream: a #GstRTSPStream
 * @rtptime: result RTP timestamp
 * @seq: result RTP seqnum
 *
 * Retrieve the current rtptime and seq. This is used to
 * construct a RTPInfo reply header.
 *
 * Returns: %TRUE when rtptime and seq could be determined.
 */
gboolean
gst_rtsp_stream_get_rtpinfo (GstRTSPStream * stream,
    guint * rtptime, guint * seq)
{
  GObjectClass *payobjclass;

  payobjclass = G_OBJECT_GET_CLASS (stream->payloader);

  if (!g_object_class_find_property (payobjclass, "seqnum") ||
      !g_object_class_find_property (payobjclass, "timestamp"))
    return FALSE;

  g_object_get (stream->payloader, "seqnum", seq, "timestamp", rtptime, NULL);

  return TRUE;
}

/**
 * gst_rtsp_stream_recv_rtp:
 * @stream: a #GstRTSPStream
 * @buffer: (transfer full): a #GstBuffer
 *
 * Handle an RTP buffer for the stream. This method is usually called when a
 * message has been received from a client using the TCP transport.
 *
 * This function takes ownership of @buffer.
 *
 * Returns: a GstFlowReturn.
 */
GstFlowReturn
gst_rtsp_stream_recv_rtp (GstRTSPStream * stream, GstBuffer * buffer)
{
  GstFlowReturn ret;

  ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (stream->appsrc[0]), buffer);

  return ret;
}

/**
 * gst_rtsp_stream_recv_rtcp:
 * @stream: a #GstRTSPStream
 * @buffer: (transfer full): a #GstBuffer
 *
 * Handle an RTCP buffer for the stream. This method is usually called when a
 * message has been received from a client using the TCP transport.
 *
 * This function takes ownership of @buffer.
 *
 * Returns: a GstFlowReturn.
 */
GstFlowReturn
gst_rtsp_stream_recv_rtcp (GstRTSPStream * stream, GstBuffer * buffer)
{
  GstFlowReturn ret;

  ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (stream->appsrc[1]), buffer);

  return ret;
}

static gboolean
update_transport (GstRTSPStream * stream, GstRTSPStreamTransport * trans,
    gboolean add)
{
  GstRTSPTransport *tr;
  gboolean updated;

  updated = FALSE;

  tr = trans->transport;

  switch (tr->lower_transport) {
    case GST_RTSP_LOWER_TRANS_UDP:
    case GST_RTSP_LOWER_TRANS_UDP_MCAST:
    {
      gchar *dest;
      gint min, max;
      guint ttl = 0;

      dest = tr->destination;
      if (tr->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
        min = tr->port.min;
        max = tr->port.max;
        ttl = tr->ttl;
      } else {
        min = tr->client_port.min;
        max = tr->client_port.max;
      }

      if (add && !trans->active) {
        GST_INFO ("adding %s:%d-%d", dest, min, max);
        g_signal_emit_by_name (stream->udpsink[0], "add", dest, min, NULL);
        g_signal_emit_by_name (stream->udpsink[1], "add", dest, max, NULL);
        if (ttl > 0) {
          GST_INFO ("setting ttl-mc %d", ttl);
          g_object_set (G_OBJECT (stream->udpsink[0]), "ttl-mc", ttl, NULL);
          g_object_set (G_OBJECT (stream->udpsink[1]), "ttl-mc", ttl, NULL);
        }
        stream->transports = g_list_prepend (stream->transports, trans);
        trans->active = TRUE;
        updated = TRUE;
      } else if (trans->active) {
        GST_INFO ("removing %s:%d-%d", dest, min, max);
        g_signal_emit_by_name (stream->udpsink[0], "remove", dest, min, NULL);
        g_signal_emit_by_name (stream->udpsink[1], "remove", dest, max, NULL);
        stream->transports = g_list_remove (stream->transports, trans);
        trans->active = FALSE;
        updated = TRUE;
      }
      break;
    }
    case GST_RTSP_LOWER_TRANS_TCP:
      if (add && !trans->active) {
        GST_INFO ("adding TCP %s", tr->destination);
        stream->transports = g_list_prepend (stream->transports, trans);
        trans->active = TRUE;
        updated = TRUE;
      } else if (trans->active) {
        GST_INFO ("removing TCP %s", tr->destination);
        stream->transports = g_list_remove (stream->transports, trans);
        trans->active = FALSE;
        updated = TRUE;
      }
      break;
    default:
      GST_INFO ("Unknown transport %d", tr->lower_transport);
      break;
  }
  return updated;
}


/**
 * gst_rtsp_stream_add_transport:
 * @stream: a #GstRTSPStream
 * @trans: a #GstRTSPStreamTransport
 *
 * Add the transport in @trans to @stream. The media of @stream will
 * then also be send to the values configured in @trans.
 *
 * @trans must contain a valid #GstRTSPTransport.
 *
 * Returns: %TRUE if @trans was added
 */
gboolean
gst_rtsp_stream_add_transport (GstRTSPStream * stream,
    GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);
  g_return_val_if_fail (trans->transport != NULL, FALSE);

  return update_transport (stream, trans, TRUE);
}

/**
 * gst_rtsp_stream_remove_transport:
 * @stream: a #GstRTSPStream
 * @trans: a #GstRTSPStreamTransport
 *
 * Remove the transport in @trans from @stream. The media of @stream will
 * not be sent to the values configured in @trans.
 *
 * Returns: %TRUE if @trans was removed
 */
gboolean
gst_rtsp_stream_remove_transport (GstRTSPStream * stream,
    GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);
  g_return_val_if_fail (trans->transport != NULL, FALSE);

  return update_transport (stream, trans, FALSE);
}
