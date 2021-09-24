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
 * With gst_rtsp_stream_transport_set_active() the transports are added and
 * removed from the stream.
 *
 * A #GstRTSPStream will call gst_rtsp_stream_transport_keep_alive() when RTCP
 * is received from the client. It will also call
 * gst_rtsp_stream_transport_set_timed_out() when a receiver has timed out.
 *
 * A #GstRTSPClient will call gst_rtsp_stream_transport_message_sent() when it
 * has sent a data message for the transport.
 *
 * Last reviewed on 2013-07-16 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include "rtsp-stream-transport.h"
#include "rtsp-server-internal.h"

struct _GstRTSPStreamTransportPrivate
{
  GstRTSPStream *stream;

  GstRTSPSendFunc send_rtp;
  GstRTSPSendFunc send_rtcp;
  gpointer user_data;
  GDestroyNotify notify;

  GstRTSPSendListFunc send_rtp_list;
  GstRTSPSendListFunc send_rtcp_list;
  gpointer list_user_data;
  GDestroyNotify list_notify;

  GstRTSPBackPressureFunc back_pressure_func;
  gpointer back_pressure_func_data;
  GDestroyNotify back_pressure_func_notify;

  GstRTSPKeepAliveFunc keep_alive;
  gpointer ka_user_data;
  GDestroyNotify ka_notify;
  gboolean timed_out;

  GstRTSPMessageSentFunc message_sent;
  gpointer ms_user_data;
  GDestroyNotify ms_notify;

  GstRTSPMessageSentFuncFull message_sent_full;
  gpointer msf_user_data;
  GDestroyNotify msf_notify;

  GstRTSPTransport *transport;
  GstRTSPUrl *url;

  GObject *rtpsource;

  /* TCP backlog */
  GstClockTime first_rtp_timestamp;
  GstQueueArray *items;
  GRecMutex backlog_lock;
};

#define MAX_BACKLOG_DURATION (10 * GST_SECOND)
#define MAX_BACKLOG_SIZE 100

typedef struct
{
  GstBuffer *buffer;
  GstBufferList *buffer_list;
  gboolean is_rtp;
} BackLogItem;


enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_stream_transport_debug);
#define GST_CAT_DEFAULT rtsp_stream_transport_debug

static void gst_rtsp_stream_transport_finalize (GObject * obj);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPStreamTransport, gst_rtsp_stream_transport,
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
clear_backlog_item (BackLogItem * item)
{
  gst_clear_buffer (&item->buffer);
  gst_clear_buffer_list (&item->buffer_list);
}

static void
gst_rtsp_stream_transport_init (GstRTSPStreamTransport * trans)
{
  trans->priv = gst_rtsp_stream_transport_get_instance_private (trans);
  trans->priv->items = gst_queue_array_new_for_struct (sizeof (BackLogItem), 0);
  trans->priv->first_rtp_timestamp = GST_CLOCK_TIME_NONE;
  gst_queue_array_set_clear_func (trans->priv->items,
      (GDestroyNotify) clear_backlog_item);
  g_rec_mutex_init (&trans->priv->backlog_lock);
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
  gst_rtsp_stream_transport_set_message_sent (trans, NULL, NULL, NULL);

  if (priv->stream)
    g_object_unref (priv->stream);

  if (priv->transport)
    gst_rtsp_transport_free (priv->transport);

  if (priv->url)
    gst_rtsp_url_free (priv->url);

  gst_queue_array_free (priv->items);

  g_rec_mutex_clear (&priv->backlog_lock);

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
  priv->stream = g_object_ref (priv->stream);
  priv->transport = tr;

  return trans;
}

/**
 * gst_rtsp_stream_transport_get_stream:
 * @trans: a #GstRTSPStreamTransport
 *
 * Get the #GstRTSPStream used when constructing @trans.
 *
 * Returns: (transfer none) (nullable): the stream used when constructing @trans.
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
 * gst_rtsp_stream_transport_set_list_callbacks:
 * @trans: a #GstRTSPStreamTransport
 * @send_rtp_list: (scope notified): a callback called when RTP should be sent
 * @send_rtcp_list: (scope notified): a callback called when RTCP should be sent
 * @user_data: (closure): user data passed to callbacks
 * @notify: (allow-none): called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when data for a stream should be sent
 * to a client. This is usually used when sending RTP/RTCP over TCP.
 *
 * Since: 1.16
 */
void
gst_rtsp_stream_transport_set_list_callbacks (GstRTSPStreamTransport * trans,
    GstRTSPSendListFunc send_rtp_list, GstRTSPSendListFunc send_rtcp_list,
    gpointer user_data, GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->send_rtp_list = send_rtp_list;
  priv->send_rtcp_list = send_rtcp_list;
  if (priv->list_notify)
    priv->list_notify (priv->list_user_data);
  priv->list_user_data = user_data;
  priv->list_notify = notify;
}

void
gst_rtsp_stream_transport_set_back_pressure_callback (GstRTSPStreamTransport *
    trans, GstRTSPBackPressureFunc back_pressure_func, gpointer user_data,
    GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->back_pressure_func = back_pressure_func;
  if (priv->back_pressure_func_notify)
    priv->back_pressure_func_notify (priv->back_pressure_func_data);
  priv->back_pressure_func_data = user_data;
  priv->back_pressure_func_notify = notify;
}

gboolean
gst_rtsp_stream_transport_check_back_pressure (GstRTSPStreamTransport * trans,
    gboolean is_rtp)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean ret = FALSE;
  guint8 channel;

  priv = trans->priv;

  if (is_rtp)
    channel = priv->transport->interleaved.min;
  else
    channel = priv->transport->interleaved.max;

  if (priv->back_pressure_func)
    ret = priv->back_pressure_func (channel, priv->back_pressure_func_data);

  return ret;
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
 * gst_rtsp_stream_transport_set_message_sent:
 * @trans: a #GstRTSPStreamTransport
 * @message_sent: (scope notified): a callback called when a message has been sent
 * @user_data: (closure): user data passed to callback
 * @notify: (allow-none): called with the user_data when no longer needed
 *
 * Install a callback that will be called when a message has been sent on @trans.
 */
void
gst_rtsp_stream_transport_set_message_sent (GstRTSPStreamTransport * trans,
    GstRTSPMessageSentFunc message_sent, gpointer user_data,
    GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->message_sent = message_sent;
  if (priv->ms_notify)
    priv->ms_notify (priv->ms_user_data);
  priv->ms_user_data = user_data;
  priv->ms_notify = notify;
}

/**
 * gst_rtsp_stream_transport_set_message_sent_full:
 * @trans: a #GstRTSPStreamTransport
 * @message_sent: (scope notified): a callback called when a message has been sent
 * @user_data: (closure): user data passed to callback
 * @notify: (allow-none): called with the user_data when no longer needed
 *
 * Install a callback that will be called when a message has been sent on @trans.
 *
 * Since: 1.18
 */
void
gst_rtsp_stream_transport_set_message_sent_full (GstRTSPStreamTransport * trans,
    GstRTSPMessageSentFuncFull message_sent, gpointer user_data,
    GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->message_sent_full = message_sent;
  if (priv->msf_notify)
    priv->msf_notify (priv->msf_user_data);
  priv->msf_user_data = user_data;
  priv->msf_notify = notify;
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
 * Returns: (transfer none) (nullable): the transport configured in @trans. It remains
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
 * @url: (transfer none) (nullable): a client #GstRTSPUrl
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
 * Returns: (transfer none) (nullable): the url configured in @trans.
 * It remains valid for as long as @trans is valid.
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

  if (!gst_rtsp_stream_is_sender (priv->stream))
    return NULL;
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

  if (active)
    res = gst_rtsp_stream_add_transport (priv->stream, trans);
  else
    res = gst_rtsp_stream_remove_transport (priv->stream, trans);

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

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  priv = trans->priv;

  if (priv->send_rtp)
    res =
        priv->send_rtp (buffer, priv->transport->interleaved.min,
        priv->user_data);

  if (res)
    gst_rtsp_stream_transport_keep_alive (trans);

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

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  priv = trans->priv;

  if (priv->send_rtcp)
    res =
        priv->send_rtcp (buffer, priv->transport->interleaved.max,
        priv->user_data);

  if (res)
    gst_rtsp_stream_transport_keep_alive (trans);

  return res;
}

/**
 * gst_rtsp_stream_transport_send_rtp_list:
 * @trans: a #GstRTSPStreamTransport
 * @buffer_list: (transfer none): a #GstBufferList
 *
 * Send @buffer_list to the installed RTP callback for @trans.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_stream_transport_send_rtp_list (GstRTSPStreamTransport * trans,
    GstBufferList * buffer_list)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res = FALSE;

  g_return_val_if_fail (GST_IS_BUFFER_LIST (buffer_list), FALSE);

  priv = trans->priv;

  if (priv->send_rtp_list) {
    res =
        priv->send_rtp_list (buffer_list, priv->transport->interleaved.min,
        priv->list_user_data);
  } else if (priv->send_rtp) {
    guint n = gst_buffer_list_length (buffer_list), i;

    for (i = 0; i < n; i++) {
      GstBuffer *buffer = gst_buffer_list_get (buffer_list, i);

      res =
          priv->send_rtp (buffer, priv->transport->interleaved.min,
          priv->user_data);
      if (!res)
        break;
    }
  }

  if (res)
    gst_rtsp_stream_transport_keep_alive (trans);

  return res;
}

/**
 * gst_rtsp_stream_transport_send_rtcp_list:
 * @trans: a #GstRTSPStreamTransport
 * @buffer_list: (transfer none): a #GstBuffer
 *
 * Send @buffer_list to the installed RTCP callback for @trans.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_stream_transport_send_rtcp_list (GstRTSPStreamTransport * trans,
    GstBufferList * buffer_list)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res = FALSE;

  g_return_val_if_fail (GST_IS_BUFFER_LIST (buffer_list), FALSE);

  priv = trans->priv;

  if (priv->send_rtcp_list) {
    res =
        priv->send_rtcp_list (buffer_list, priv->transport->interleaved.max,
        priv->list_user_data);
  } else if (priv->send_rtcp) {
    guint n = gst_buffer_list_length (buffer_list), i;

    for (i = 0; i < n; i++) {
      GstBuffer *buffer = gst_buffer_list_get (buffer_list, i);

      res =
          priv->send_rtcp (buffer, priv->transport->interleaved.max,
          priv->user_data);
      if (!res)
        break;
    }
  }

  if (res)
    gst_rtsp_stream_transport_keep_alive (trans);

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

/**
 * gst_rtsp_stream_transport_message_sent:
 * @trans: a #GstRTSPStreamTransport
 *
 * Signal the installed message_sent / message_sent_full callback for @trans.
 *
 * Since: 1.16
 */
void
gst_rtsp_stream_transport_message_sent (GstRTSPStreamTransport * trans)
{
  GstRTSPStreamTransportPrivate *priv;

  priv = trans->priv;

  if (priv->message_sent_full)
    priv->message_sent_full (trans, priv->msf_user_data);
  if (priv->message_sent)
    priv->message_sent (priv->ms_user_data);
}

/**
 * gst_rtsp_stream_transport_recv_data:
 * @trans: a #GstRTSPStreamTransport
 * @channel: a channel
 * @buffer: (transfer full): a #GstBuffer
 *
 * Receive @buffer on @channel @trans.
 *
 * Returns: a #GstFlowReturn. Returns GST_FLOW_NOT_LINKED when @channel is not
 *    configured in the transport of @trans.
 */
GstFlowReturn
gst_rtsp_stream_transport_recv_data (GstRTSPStreamTransport * trans,
    guint channel, GstBuffer * buffer)
{
  GstRTSPStreamTransportPrivate *priv;
  const GstRTSPTransport *tr;
  GstFlowReturn res;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  priv = trans->priv;
  tr = priv->transport;

  if (tr->interleaved.min == channel) {
    res = gst_rtsp_stream_recv_rtp (priv->stream, buffer);
  } else if (tr->interleaved.max == channel) {
    res = gst_rtsp_stream_recv_rtcp (priv->stream, buffer);
  } else {
    res = GST_FLOW_NOT_LINKED;
  }
  return res;
}

static GstClockTime
get_backlog_item_timestamp (BackLogItem * item)
{
  GstClockTime ret = GST_CLOCK_TIME_NONE;

  if (item->buffer) {
    ret = GST_BUFFER_DTS_OR_PTS (item->buffer);
  } else if (item->buffer_list) {
    g_assert (gst_buffer_list_length (item->buffer_list) > 0);
    ret = GST_BUFFER_DTS_OR_PTS (gst_buffer_list_get (item->buffer_list, 0));
  }

  return ret;
}

static GstClockTime
get_first_backlog_timestamp (GstRTSPStreamTransport * trans)
{
  GstRTSPStreamTransportPrivate *priv = trans->priv;
  GstClockTime ret = GST_CLOCK_TIME_NONE;
  guint i, l;

  l = gst_queue_array_get_length (priv->items);

  for (i = 0; i < l; i++) {
    BackLogItem *item = (BackLogItem *)
        gst_queue_array_peek_nth_struct (priv->items, i);

    if (item->is_rtp) {
      ret = get_backlog_item_timestamp (item);
      break;
    }
  }

  return ret;
}

/* Not MT-safe, caller should ensure consistent locking (see
 * gst_rtsp_stream_transport_lock_backlog()). Ownership
 * of @buffer and @buffer_list is transfered to the transport */
gboolean
gst_rtsp_stream_transport_backlog_push (GstRTSPStreamTransport * trans,
    GstBuffer * buffer, GstBufferList * buffer_list, gboolean is_rtp)
{
  gboolean ret = TRUE;
  BackLogItem item = { 0, };
  GstClockTime item_timestamp;
  GstRTSPStreamTransportPrivate *priv;

  priv = trans->priv;

  if (buffer)
    item.buffer = buffer;
  if (buffer_list)
    item.buffer_list = buffer_list;
  item.is_rtp = is_rtp;

  gst_queue_array_push_tail_struct (priv->items, &item);

  item_timestamp = get_backlog_item_timestamp (&item);

  if (is_rtp && priv->first_rtp_timestamp != GST_CLOCK_TIME_NONE) {
    GstClockTimeDiff queue_duration;

    g_assert (GST_CLOCK_TIME_IS_VALID (item_timestamp));

    queue_duration = GST_CLOCK_DIFF (priv->first_rtp_timestamp, item_timestamp);

    g_assert (queue_duration >= 0);

    if (queue_duration > MAX_BACKLOG_DURATION &&
        gst_queue_array_get_length (priv->items) > MAX_BACKLOG_SIZE) {
      ret = FALSE;
    }
  } else if (is_rtp) {
    priv->first_rtp_timestamp = item_timestamp;
  }

  return ret;
}

/* Not MT-safe, caller should ensure consistent locking (see
 * gst_rtsp_stream_transport_lock_backlog()). Ownership
 * of @buffer and @buffer_list is transfered back to the caller,
 * if either of those is NULL the underlying object is unreffed */
gboolean
gst_rtsp_stream_transport_backlog_pop (GstRTSPStreamTransport * trans,
    GstBuffer ** buffer, GstBufferList ** buffer_list, gboolean * is_rtp)
{
  BackLogItem *item;
  GstRTSPStreamTransportPrivate *priv;

  g_return_val_if_fail (!gst_rtsp_stream_transport_backlog_is_empty (trans),
      FALSE);

  priv = trans->priv;

  item = (BackLogItem *) gst_queue_array_pop_head_struct (priv->items);

  priv->first_rtp_timestamp = get_first_backlog_timestamp (trans);

  if (buffer)
    *buffer = item->buffer;
  else if (item->buffer)
    gst_buffer_unref (item->buffer);

  if (buffer_list)
    *buffer_list = item->buffer_list;
  else if (item->buffer_list)
    gst_buffer_list_unref (item->buffer_list);

  if (is_rtp)
    *is_rtp = item->is_rtp;

  return TRUE;
}

/* Not MT-safe, caller should ensure consistent locking.
 * See gst_rtsp_stream_transport_lock_backlog() */
gboolean
gst_rtsp_stream_transport_backlog_is_empty (GstRTSPStreamTransport * trans)
{
  return gst_queue_array_is_empty (trans->priv->items);
}

/* Not MT-safe, caller should ensure consistent locking.
 * See gst_rtsp_stream_transport_lock_backlog() */
void
gst_rtsp_stream_transport_clear_backlog (GstRTSPStreamTransport * trans)
{
  while (!gst_rtsp_stream_transport_backlog_is_empty (trans)) {
    gst_rtsp_stream_transport_backlog_pop (trans, NULL, NULL, NULL);
  }
}

/* Internal API, protects access to the TCP backlog. Safe to
 * call recursively */
void
gst_rtsp_stream_transport_lock_backlog (GstRTSPStreamTransport * trans)
{
  g_rec_mutex_lock (&trans->priv->backlog_lock);
}

/* See gst_rtsp_stream_transport_lock_backlog() */
void
gst_rtsp_stream_transport_unlock_backlog (GstRTSPStreamTransport * trans)
{
  g_rec_mutex_unlock (&trans->priv->backlog_lock);
}
