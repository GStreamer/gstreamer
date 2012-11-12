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
#include <stdlib.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include "rtsp-stream-transport.h"

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_stream_transport_debug);
#define GST_CAT_DEFAULT rtsp_stream_transport_debug

static void gst_rtsp_stream_transport_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPStreamTransport, gst_rtsp_stream_transport,
    G_TYPE_OBJECT);

static void
gst_rtsp_stream_transport_class_init (GstRTSPStreamTransportClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_stream_transport_finalize;

  GST_DEBUG_CATEGORY_INIT (rtsp_stream_transport_debug, "rtspmediatransport",
      0, "GstRTSPStreamTransport");
}

static void
gst_rtsp_stream_transport_init (GstRTSPStreamTransport * trans)
{
}

static void
gst_rtsp_stream_transport_finalize (GObject * obj)
{
  GstRTSPStreamTransport *trans;

  trans = GST_RTSP_STREAM_TRANSPORT (obj);

  /* remove callbacks now */
  gst_rtsp_stream_transport_set_callbacks (trans, NULL, NULL, NULL, NULL);
  gst_rtsp_stream_transport_set_keepalive (trans, NULL, NULL, NULL);

  if (trans->transport)
    gst_rtsp_transport_free (trans->transport);

#if 0
  if (trans->rtpsource)
    g_object_set_qdata (trans->rtpsource, ssrc_stream_map_key, NULL);
#endif

  G_OBJECT_CLASS (gst_rtsp_stream_transport_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_stream_transport_new:
 * @stream: a #GstRTSPStream
 * @tr: (transfer full): a GstRTSPTransport
 *
 * Create a new #GstRTSPStreamTransport that can be used to manage
 * @stream with transport @tr.
 *
 * Returns: a new #GstRTSPStreamTransport
 */
GstRTSPStreamTransport *
gst_rtsp_stream_transport_new (GstRTSPStream * stream, GstRTSPTransport * tr)
{
  GstRTSPStreamTransport *trans;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (tr != NULL, NULL);

  trans = g_object_new (GST_TYPE_RTSP_STREAM_TRANSPORT, NULL);
  trans->stream = stream;
  trans->transport = tr;

  return trans;
}

/**
 * gst_rtsp_stream_transport_set_callbacks:
 * @trans: a #GstRTSPStreamTransport
 * @send_rtp: (scope notified): a callback called when RTP should be sent
 * @send_rtcp: (scope notified): a callback called when RTCP should be sent
 * @user_data: user data passed to callbacks
 * @notify: called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when data for a stream should be sent
 * to a client. This is usually used when sending RTP/RTCP over TCP.
 */
void
gst_rtsp_stream_transport_set_callbacks (GstRTSPStreamTransport * trans,
    GstRTSPSendFunc send_rtp, GstRTSPSendFunc send_rtcp,
    gpointer user_data, GDestroyNotify notify)
{
  trans->send_rtp = send_rtp;
  trans->send_rtcp = send_rtcp;
  if (trans->notify)
    trans->notify (trans->user_data);
  trans->user_data = user_data;
  trans->notify = notify;
}

/**
 * gst_rtsp_stream_transport_set_keepalive:
 * @trans: a #GstRTSPStreamTransport
 * @keep_alive: a callback called when the receiver is active
 * @user_data: user data passed to callback
 * @notify: called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when RTCP packets are received from the
 * receiver of @trans.
 */
void
gst_rtsp_stream_transport_set_keepalive (GstRTSPStreamTransport * trans,
    GstRTSPKeepAliveFunc keep_alive, gpointer user_data, GDestroyNotify notify)
{
  trans->keep_alive = keep_alive;
  if (trans->ka_notify)
    trans->ka_notify (trans->ka_user_data);
  trans->ka_user_data = user_data;
  trans->ka_notify = notify;
}


/**
 * gst_rtsp_stream_transport_set_transport:
 * @trans: a #GstRTSPStreamTransport
 * @tr: (transfer full): a client #GstRTSPTransport
 *
 * Set @ct as the client transport. This function takes ownership of
 * the passed @tr.
 */
void
gst_rtsp_stream_transport_set_transport (GstRTSPStreamTransport * trans,
    GstRTSPTransport * tr)
{
  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));
  g_return_if_fail (tr != NULL);

  /* keep track of the transports in the stream. */
  if (trans->transport)
    gst_rtsp_transport_free (trans->transport);
  trans->transport = tr;
}

/**
 * gst_rtsp_stream_transport_send_rtp:
 * @trans: a #GstRTSPStreamTransport
 * @buffer: a #GstBuffer
 *
 * Send @buffer to the installed RTP callback for @trans.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_transport_send_rtp (GstRTSPStreamTransport * trans,
    GstBuffer * buffer)
{
  gboolean res = FALSE;

  if (trans->send_rtp)
    res =
        trans->send_rtp (buffer, trans->transport->interleaved.min,
        trans->user_data);

  return res;
}

/**
 * gst_rtsp_stream_transport_send_rtcp:
 * @trans: a #GstRTSPStreamTransport
 * @buffer: a #GstBuffer
 *
 * Send @buffer to the installed RTCP callback for @trans.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_transport_send_rtcp (GstRTSPStreamTransport * trans,
    GstBuffer * buffer)
{
  gboolean res = FALSE;

  if (trans->send_rtcp)
    res =
        trans->send_rtcp (buffer, trans->transport->interleaved.max,
        trans->user_data);

  return res;
}
