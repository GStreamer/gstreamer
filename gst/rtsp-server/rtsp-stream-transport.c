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
/**
 * SECTION:rtsp-stream-transport
 * @short_description: A media stream transport configuration
 * @see_also: #GstRTSPStream, #GstRTSPSessionMedia
 *
 * The #GstRTSPStreamTransport configures the transport used by a
 * #GstRTSPStream. It is usually manages by a #GstRTSPSessionMedia object.
 *
 * With gst_rtsp_stream_transport_set_callbacks(), callbacks can be configured
 * to handle the RTP and RTCP packets from the stream, for example when they
 * need to be sent over TCP.
 *
 * With  gst_rtsp_stream_transport_set_active() the transports are added and
 * removed from the stream.
 *
 * A #GstRTSPStream will call gst_rtsp_stream_transport_keep_alive() when RTCP
 * is received from the client. It will also call
 * gst_rtsp_stream_transport_set_timed_out() when a receiver has timed out.
 *
 * Last reviewed on 2013-07-16 (1.0.0)
 */

#include <string.h>
#include <stdlib.h>

#include "rtsp-stream-transport.h"

#define GST_RTSP_STREAM_TRANSPORT_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_STREAM_TRANSPORT, GstRTSPStreamTransportPrivate))

struct _GstRTSPStreamTransportPrivate
{
  GstRTSPStream *stream;

  GstRTSPSendFunc send_rtp;
  GstRTSPSendFunc send_rtcp;
  gpointer user_data;
  GDestroyNotify notify;

  GstRTSPKeepAliveFunc keep_alive;
  gpointer ka_user_data;
  GDestroyNotify ka_notify;
  gboolean active;
  gboolean timed_out;

  GstRTSPTransport *transport;
  GstRTSPUrl *url;

  GObject *rtpsource;
};

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

  g_type_class_add_private (klass, sizeof (GstRTSPStreamTransportPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_stream_transport_finalize;

  GST_DEBUG_CATEGORY_INIT (rtsp_stream_transport_debug, "rtspmediatransport",
      0, "GstRTSPStreamTransport");
}

static void
gst_rtsp_stream_transport_init (GstRTSPStreamTransport * trans)
{
  GstRTSPStreamTransportPrivate *priv =
      GST_RTSP_STREAM_TRANSPORT_GET_PRIVATE (trans);

  trans->priv = priv;
}

static void
gst_rtsp_stream_transport_finalize (GObject * obj)
{
  GstRTSPStreamTransportPrivate *priv;
  GstRTSPStreamTransport *trans;

  trans = GST_RTSP_STREAM_TRANSPORT (obj);
  priv = trans->priv;

  /* remove callbacks now */
  gst_rtsp_stream_transport_set_callbacks (trans, NULL, NULL, NULL, NULL);
  gst_rtsp_stream_transport_set_keepalive (trans, NULL, NULL, NULL);

  if (priv->transport)
    gst_rtsp_transport_free (priv->transport);

  if (priv->url)
    gst_rtsp_url_free (priv->url);

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
 * Returns: (transfer full): a new #GstRTSPStreamTransport
 */
GstRTSPStreamTransport *
gst_rtsp_stream_transport_new (GstRTSPStream * stream, GstRTSPTransport * tr)
{
  GstRTSPStreamTransportPrivate *priv;
  GstRTSPStreamTransport *trans;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (tr != NULL, NULL);

  trans = g_object_new (GST_TYPE_RTSP_STREAM_TRANSPORT, NULL);
  priv = trans->priv;
  priv->stream = stream;
  priv->transport = tr;

  return trans;
}

/**
 * gst_rtsp_stream_transport_get_stream:
 * @trans: a #GstRTSPStreamTransport
 *
 * Get the #GstRTSPStream used when constructing @trans.
 *
 * Returns: (transfer none): the stream used when constructing @trans.
 */
GstRTSPStream *
gst_rtsp_stream_transport_get_stream (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), NULL);

  return trans->priv->stream;
}

/**
 * gst_rtsp_stream_transport_set_callbacks:
 * @trans: a #GstRTSPStreamTransport
 * @send_rtp: (scope notified): a callback called when RTP should be sent
 * @send_rtcp: (scope notified): a callback called when RTCP should be sent
 * @user_data: (closure): user data passed to callbacks
 * @notify: (allow-none): called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when data for a stream should be sent
 * to a client. This is usually used when sending RTP/RTCP over TCP.
 */
void
gst_rtsp_stream_transport_set_callbacks (GstRTSPStreamTransport * trans,
    GstRTSPSendFunc send_rtp, GstRTSPSendFunc send_rtcp,
    gpointer user_data, GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->send_rtp = send_rtp;
  priv->send_rtcp = send_rtcp;
  if (priv->notify)
    priv->notify (priv->user_data);
  priv->user_data = user_data;
  priv->notify = notify;
}

/**
 * gst_rtsp_stream_transport_set_keepalive:
 * @trans: a #GstRTSPStreamTransport
 * @keep_alive: (scope notified): a callback called when the receiver is active
 * @user_data: (closure): user data passed to callback
 * @notify: (allow-none): called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when RTCP packets are received from the
 * receiver of @trans.
 */
void
gst_rtsp_stream_transport_set_keepalive (GstRTSPStreamTransport * trans,
    GstRTSPKeepAliveFunc keep_alive, gpointer user_data, GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->keep_alive = keep_alive;
  if (priv->ka_notify)
    priv->ka_notify (priv->ka_user_data);
  priv->ka_user_data = user_data;
  priv->ka_notify = notify;
}


/**
 * gst_rtsp_stream_transport_set_transport:
 * @trans: a #GstRTSPStreamTransport
 * @tr: (transfer full): a client #GstRTSPTransport
 *
 * Set @tr as the client transport. This function takes ownership of the
 * passed @tr.
 */
void
gst_rtsp_stream_transport_set_transport (GstRTSPStreamTransport * trans,
    GstRTSPTransport * tr)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));
  g_return_if_fail (tr != NULL);

  priv = trans->priv;

  /* keep track of the transports in the stream. */
  if (priv->transport)
    gst_rtsp_transport_free (priv->transport);
  priv->transport = tr;
}

/**
 * gst_rtsp_stream_transport_get_transport:
 * @trans: a #GstRTSPStreamTransport
 *
 * Get the transport configured in @trans.
 *
 * Returns: (transfer none): the transport configured in @trans. It remains
 * valid for as long as @trans is valid.
 */
const GstRTSPTransport *
gst_rtsp_stream_transport_get_transport (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), NULL);

  return trans->priv->transport;
}

/**
 * gst_rtsp_stream_transport_set_url:
 * @trans: a #GstRTSPStreamTransport
 * @url: (transfer none): a client #GstRTSPUrl
 *
 * Set @url as the client url.
 */
void
gst_rtsp_stream_transport_set_url (GstRTSPStreamTransport * trans,
    const GstRTSPUrl * url)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  /* keep track of the transports in the stream. */
  if (priv->url)
    gst_rtsp_url_free (priv->url);
  priv->url = (url ? gst_rtsp_url_copy (url) : NULL);
}

/**
 * gst_rtsp_stream_transport_get_url:
 * @trans: a #GstRTSPStreamTransport
 *
 * Get the url configured in @trans.
 *
 * Returns: (transfer none): the url configured in @trans. It remains
 * valid for as long as @trans is valid.
 */
const GstRTSPUrl *
gst_rtsp_stream_transport_get_url (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), NULL);

  return trans->priv->url;
}

 /**
 * gst_rtsp_stream_transport_get_rtpinfo:
 * @trans: a #GstRTSPStreamTransport
 * @start_time: a star time
 *
 * Get the RTP-Info string for @trans and @start_time.
 *
 * Returns: (transfer full) (nullable): the RTPInfo string for @trans
 * and @start_time or %NULL when the RTP-Info could not be
 * determined. g_free() after usage.
 */
gchar *
gst_rtsp_stream_transport_get_rtpinfo (GstRTSPStreamTransport * trans,
    GstClockTime start_time)
{
  GstRTSPStreamTransportPrivate *priv;
  gchar *url_str;
  GString *rtpinfo;
  guint rtptime, seq, clock_rate;
  GstClockTime running_time = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), NULL);

  priv = trans->priv;

  if (!gst_rtsp_stream_get_rtpinfo (priv->stream, &rtptime, &seq, &clock_rate,
          &running_time))
    return NULL;

  GST_DEBUG ("RTP time %u, seq %u, rate %u, running-time %" GST_TIME_FORMAT,
      rtptime, seq, clock_rate, GST_TIME_ARGS (running_time));

  if (GST_CLOCK_TIME_IS_VALID (running_time)
      && GST_CLOCK_TIME_IS_VALID (start_time)) {
    if (running_time > start_time) {
      rtptime -=
          gst_util_uint64_scale_int (running_time - start_time, clock_rate,
          GST_SECOND);
    } else {
      rtptime +=
          gst_util_uint64_scale_int (start_time - running_time, clock_rate,
          GST_SECOND);
    }
  }
  GST_DEBUG ("RTP time %u, for start-time %" GST_TIME_FORMAT,
      rtptime, GST_TIME_ARGS (start_time));

  rtpinfo = g_string_new ("");

  url_str = gst_rtsp_url_get_request_uri (trans->priv->url);
  g_string_append_printf (rtpinfo, "url=%s;seq=%u;rtptime=%u",
      url_str, seq, rtptime);
  g_free (url_str);

  return g_string_free (rtpinfo, FALSE);
}

/**
 * gst_rtsp_stream_transport_set_active:
 * @trans: a #GstRTSPStreamTransport
 * @active: new state of @trans
 *
 * Activate or deactivate datatransfer configured in @trans.
 *
 * Returns: %TRUE when the state was changed.
 */
gboolean
gst_rtsp_stream_transport_set_active (GstRTSPStreamTransport * trans,
    gboolean active)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);

  priv = trans->priv;

  if (priv->active == active)
    return FALSE;

  if (active)
    res = gst_rtsp_stream_add_transport (priv->stream, trans);
  else
    res = gst_rtsp_stream_remove_transport (priv->stream, trans);

  if (res)
    priv->active = active;

  return res;
}

/**
 * gst_rtsp_stream_transport_set_timed_out:
 * @trans: a #GstRTSPStreamTransport
 * @timedout: timed out value
 *
 * Set the timed out state of @trans to @timedout
 */
void
gst_rtsp_stream_transport_set_timed_out (GstRTSPStreamTransport * trans,
    gboolean timedout)
{
  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  trans->priv->timed_out = timedout;
}

/**
 * gst_rtsp_stream_transport_is_timed_out:
 * @trans: a #GstRTSPStreamTransport
 *
 * Check if @trans is timed out.
 *
 * Returns: %TRUE if @trans timed out.
 */
gboolean
gst_rtsp_stream_transport_is_timed_out (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);

  return trans->priv->timed_out;
}

/**
 * gst_rtsp_stream_transport_send_rtp:
 * @trans: a #GstRTSPStreamTransport
 * @buffer: (transfer none): a #GstBuffer
 *
 * Send @buffer to the installed RTP callback for @trans.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_transport_send_rtp (GstRTSPStreamTransport * trans,
    GstBuffer * buffer)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res = FALSE;

  priv = trans->priv;

  if (priv->send_rtp)
    res =
        priv->send_rtp (buffer, priv->transport->interleaved.min,
        priv->user_data);

  return res;
}

/**
 * gst_rtsp_stream_transport_send_rtcp:
 * @trans: a #GstRTSPStreamTransport
 * @buffer: (transfer none): a #GstBuffer
 *
 * Send @buffer to the installed RTCP callback for @trans.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_transport_send_rtcp (GstRTSPStreamTransport * trans,
    GstBuffer * buffer)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res = FALSE;

  priv = trans->priv;

  if (priv->send_rtcp)
    res =
        priv->send_rtcp (buffer, priv->transport->interleaved.max,
        priv->user_data);

  return res;
}

/**
 * gst_rtsp_stream_transport_keep_alive:
 * @trans: a #GstRTSPStreamTransport
 *
 * Signal the installed keep_alive callback for @trans.
 */
void
gst_rtsp_stream_transport_keep_alive (GstRTSPStreamTransport * trans)
{
  GstRTSPStreamTransportPrivate *priv;

  priv = trans->priv;

  if (priv->keep_alive)
    priv->keep_alive (priv->ka_user_data);
}
