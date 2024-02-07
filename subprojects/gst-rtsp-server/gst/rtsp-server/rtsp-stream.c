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
 * SECTION:rtsp-stream
 * @short_description: A media stream
 * @see_also: #GstRTSPMedia
 *
 * The #GstRTSPStream object manages the data transport for one stream. It
 * is created from a payloader element and a source pad that produce the RTP
 * packets for the stream.
 *
 * With gst_rtsp_stream_join_bin() the streaming elements are added to the bin
 * and rtpbin. gst_rtsp_stream_leave_bin() removes the elements again.
 *
 * The #GstRTSPStream will use the configured addresspool, as set with
 * gst_rtsp_stream_set_address_pool(), to allocate multicast addresses for the
 * stream. With gst_rtsp_stream_get_multicast_address() you can get the
 * configured address.
 *
 * With gst_rtsp_stream_get_server_port () you can get the port that the server
 * will use to receive RTCP. This is the part that the clients will use to send
 * RTCP to.
 *
 * With gst_rtsp_stream_add_transport() destinations can be added where the
 * stream should be sent to. Use gst_rtsp_stream_remove_transport() to remove
 * the destination again.
 *
 * Each #GstRTSPStreamTransport spawns one queue that will serve as a backlog of a
 * controllable maximum size when the reflux from the TCP connection's backpressure
 * starts spilling all over.
 *
 * Unlike the backlog in rtspconnection, which we have decided should only contain
 * at most one RTP and one RTCP data message in order to allow control messages to
 * go through unobstructed, this backlog only consists of data messages, allowing
 * us to fill it up without concern.
 *
 * When multiple TCP transports exist, for example in the context of a shared media,
 * we only pop samples from our appsinks when at least one of the transports doesn't
 * experience back pressure: this allows us to pace our sample popping to the speed
 * of the fastest client.
 *
 * When a sample is popped, it is either sent directly on transports that don't
 * experience backpressure, or queued on the transport's backlog otherwise. Samples
 * are then popped from that backlog when the transport reports it has sent the message.
 *
 * Once the backlog reaches an overly large duration, the transport is dropped as
 * the client was deemed too slow.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gio/gio.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "rtsp-stream.h"
#include "rtsp-server-internal.h"

struct _GstRTSPStreamPrivate
{
  GMutex lock;
  guint idx;
  /* Only one pad is ever set */
  GstPad *srcpad, *sinkpad;
  GstElement *payloader;
  guint buffer_size;
  GstBin *joined_bin;

  /* TRUE if this stream is running on
   * the client side of an RTSP link (for RECORD) */
  gboolean client_side;
  gchar *control;

  /* TRUE if stream is complete. This means that the receiver and the sender
   * parts are present in the stream. */
  gboolean is_complete;
  GstRTSPProfile profiles;
  GstRTSPLowerTrans allowed_protocols;
  GstRTSPLowerTrans configured_protocols;

  /* pads on the rtpbin */
  GstPad *send_rtp_sink;
  GstPad *recv_rtp_src;
  GstPad *recv_sink[2];
  GstPad *send_src[2];

  /* the RTPSession object */
  GObject *session;

  /* SRTP encoder/decoder */
  GstElement *srtpenc;
  GstElement *srtpdec;
  GHashTable *keys;

  /* for UDP unicast */
  GstElement *udpsrc_v4[2];
  GstElement *udpsrc_v6[2];
  GstElement *udpqueue[2];
  GstElement *udpsink[2];
  GSocket *socket_v4[2];
  GSocket *socket_v6[2];

  /* for UDP multicast */
  GstElement *mcast_udpsrc_v4[2];
  GstElement *mcast_udpsrc_v6[2];
  GstElement *mcast_udpqueue[2];
  GstElement *mcast_udpsink[2];
  GSocket *mcast_socket_v4[2];
  GSocket *mcast_socket_v6[2];
  GList *mcast_clients;

  /* for TCP transport */
  GstElement *appsrc[2];
  GstClockTime appsrc_base_time[2];
  GstElement *appqueue[2];
  GstElement *appsink[2];

  GstElement *tee[2];
  GstElement *funnel[2];

  /* retransmission */
  GstElement *rtxsend;
  GstElement *rtxreceive;
  guint rtx_pt;
  GstClockTime rtx_time;

  /* rate control */
  gboolean do_rate_control;

  /* Forward Error Correction with RFC 5109 */
  GstElement *ulpfec_decoder;
  GstElement *ulpfec_encoder;
  guint ulpfec_pt;
  gboolean ulpfec_enabled;
  guint ulpfec_percentage;

  /* pool used to manage unicast and multicast addresses */
  GstRTSPAddressPool *pool;

  /* unicast server addr/port */
  GstRTSPAddress *server_addr_v4;
  GstRTSPAddress *server_addr_v6;

  /* multicast addresses */
  GstRTSPAddress *mcast_addr_v4;
  GstRTSPAddress *mcast_addr_v6;

  gchar *multicast_iface;
  guint max_mcast_ttl;
  gboolean bind_mcast_address;

  /* the caps of the stream */
  gulong caps_sig;
  GstCaps *caps;

  /* transports we stream to */
  guint n_active;
  GList *transports;
  guint transports_cookie;
  GPtrArray *tr_cache;
  guint tr_cache_cookie;
  guint n_tcp_transports;
  gboolean have_buffer[2];

  gint dscp_qos;

  /* Sending logic for TCP */
  GThread *send_thread;
  GCond send_cond;
  GMutex send_lock;
  /* @send_lock is released when pushing data out, we use
   * a cookie to decide whether we should wait on @send_cond
   * before checking the transports' backlogs again
   */
  guint send_cookie;
  /* Used to control shutdown of @send_thread */
  gboolean continue_sending;

  /* stream blocking */
  gulong blocked_id[2];
  gboolean blocking;

  /* current stream postion */
  GstClockTime position;

  /* pt->caps map for RECORD streams */
  GHashTable *ptmap;

  GstRTSPPublishClockMode publish_clock_mode;
  GThreadPool *send_pool;

  /* Used to provide accurate rtpinfo when the stream is blocking */
  gboolean blocked_buffer;
  guint32 blocked_seqnum;
  guint32 blocked_rtptime;
  GstClockTime blocked_running_time;
  gint blocked_clock_rate;

  /* Whether we should send and receive RTCP */
  gboolean enable_rtcp;

  /* blocking early rtcp packets */
  GstPad *block_early_rtcp_pad;
  gulong block_early_rtcp_probe;
  GstPad *block_early_rtcp_pad_ipv6;
  gulong block_early_rtcp_probe_ipv6;

  /* set to drop delta units in blocking pad */
  gboolean drop_delta_units;

  /* used to indicate that the drop probe has dropped a buffer and should be
   * removed */
  gboolean remove_drop_probe;

};

#define DEFAULT_CONTROL         NULL
#define DEFAULT_PROFILES        GST_RTSP_PROFILE_AVP
#define DEFAULT_PROTOCOLS       GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | \
                                        GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_MAX_MCAST_TTL   255
#define DEFAULT_BIND_MCAST_ADDRESS FALSE
#define DEFAULT_DO_RATE_CONTROL TRUE
#define DEFAULT_ENABLE_RTCP TRUE

enum
{
  PROP_0,
  PROP_CONTROL,
  PROP_PROFILES,
  PROP_PROTOCOLS,
  PROP_LAST
};

enum
{
  SIGNAL_NEW_RTP_ENCODER,
  SIGNAL_NEW_RTCP_ENCODER,
  SIGNAL_NEW_RTP_RTCP_DECODER,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_stream_debug);
#define GST_CAT_DEFAULT rtsp_stream_debug

static GQuark ssrc_stream_map_key;

static void gst_rtsp_stream_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_stream_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);

static void gst_rtsp_stream_finalize (GObject * obj);

static gboolean
update_transport (GstRTSPStream * stream, GstRTSPStreamTransport * trans,
    gboolean add);

static guint gst_rtsp_stream_signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPStream, gst_rtsp_stream, G_TYPE_OBJECT);

static void
gst_rtsp_stream_class_init (GstRTSPStreamClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_stream_get_property;
  gobject_class->set_property = gst_rtsp_stream_set_property;
  gobject_class->finalize = gst_rtsp_stream_finalize;

  g_object_class_install_property (gobject_class, PROP_CONTROL,
      g_param_spec_string ("control", "Control",
          "The control string for this stream", DEFAULT_CONTROL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROFILES,
      g_param_spec_flags ("profiles", "Profiles",
          "Allowed transfer profiles", GST_TYPE_RTSP_PROFILE,
          DEFAULT_PROFILES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols",
          "Allowed lower transport protocols", GST_TYPE_RTSP_LOWER_TRANS,
          DEFAULT_PROTOCOLS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_stream_signals[SIGNAL_NEW_RTP_ENCODER] =
      g_signal_new ("new-rtp-encoder", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  gst_rtsp_stream_signals[SIGNAL_NEW_RTCP_ENCODER] =
      g_signal_new ("new-rtcp-encoder", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  gst_rtsp_stream_signals[SIGNAL_NEW_RTP_RTCP_DECODER] =
      g_signal_new ("new-rtp-rtcp-decoder", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (rtsp_stream_debug, "rtspstream", 0, "GstRTSPStream");

  ssrc_stream_map_key = g_quark_from_static_string ("GstRTSPServer.stream");
}

static void
gst_rtsp_stream_init (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = gst_rtsp_stream_get_instance_private (stream);

  GST_DEBUG ("new stream %p", stream);

  stream->priv = priv;

  priv->dscp_qos = -1;
  priv->control = g_strdup (DEFAULT_CONTROL);
  priv->profiles = DEFAULT_PROFILES;
  priv->allowed_protocols = DEFAULT_PROTOCOLS;
  priv->configured_protocols = 0;
  priv->publish_clock_mode = GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK;
  priv->max_mcast_ttl = DEFAULT_MAX_MCAST_TTL;
  priv->bind_mcast_address = DEFAULT_BIND_MCAST_ADDRESS;
  priv->do_rate_control = DEFAULT_DO_RATE_CONTROL;
  priv->enable_rtcp = DEFAULT_ENABLE_RTCP;

  g_mutex_init (&priv->lock);

  priv->continue_sending = TRUE;
  priv->send_cookie = 0;
  g_cond_init (&priv->send_cond);
  g_mutex_init (&priv->send_lock);

  priv->keys = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_caps_unref);
  priv->ptmap = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gst_caps_unref);
  priv->send_pool = NULL;
  priv->block_early_rtcp_pad = NULL;
  priv->block_early_rtcp_probe = 0;
  priv->block_early_rtcp_pad_ipv6 = NULL;
  priv->block_early_rtcp_probe_ipv6 = 0;
  priv->drop_delta_units = FALSE;
  priv->remove_drop_probe = FALSE;
}

typedef struct _UdpClientAddrInfo UdpClientAddrInfo;

struct _UdpClientAddrInfo
{
  gchar *address;
  guint rtp_port;
  guint add_count;              /* how often this address has been added */
};

static void
free_mcast_client (gpointer data)
{
  UdpClientAddrInfo *client = data;

  g_free (client->address);
  g_free (client);
}

static void
gst_rtsp_stream_finalize (GObject * obj)
{
  GstRTSPStream *stream;
  GstRTSPStreamPrivate *priv;
  guint i;

  stream = GST_RTSP_STREAM (obj);
  priv = stream->priv;

  GST_DEBUG ("finalize stream %p", stream);

  /* we really need to be unjoined now */
  g_return_if_fail (priv->joined_bin == NULL);

  if (priv->send_pool)
    g_thread_pool_free (priv->send_pool, TRUE, TRUE);
  if (priv->mcast_addr_v4)
    gst_rtsp_address_free (priv->mcast_addr_v4);
  if (priv->mcast_addr_v6)
    gst_rtsp_address_free (priv->mcast_addr_v6);
  if (priv->server_addr_v4)
    gst_rtsp_address_free (priv->server_addr_v4);
  if (priv->server_addr_v6)
    gst_rtsp_address_free (priv->server_addr_v6);
  if (priv->pool)
    g_object_unref (priv->pool);
  if (priv->rtxsend)
    g_object_unref (priv->rtxsend);
  if (priv->rtxreceive)
    g_object_unref (priv->rtxreceive);
  if (priv->ulpfec_encoder)
    gst_object_unref (priv->ulpfec_encoder);
  if (priv->ulpfec_decoder)
    gst_object_unref (priv->ulpfec_decoder);

  for (i = 0; i < 2; i++) {
    if (priv->socket_v4[i])
      g_object_unref (priv->socket_v4[i]);
    if (priv->socket_v6[i])
      g_object_unref (priv->socket_v6[i]);
    if (priv->mcast_socket_v4[i])
      g_object_unref (priv->mcast_socket_v4[i]);
    if (priv->mcast_socket_v6[i])
      g_object_unref (priv->mcast_socket_v6[i]);
  }

  g_free (priv->multicast_iface);
  g_list_free_full (priv->mcast_clients, (GDestroyNotify) free_mcast_client);

  gst_object_unref (priv->payloader);
  if (priv->srcpad)
    gst_object_unref (priv->srcpad);
  if (priv->sinkpad)
    gst_object_unref (priv->sinkpad);
  g_free (priv->control);
  g_mutex_clear (&priv->lock);

  g_hash_table_unref (priv->keys);
  g_hash_table_destroy (priv->ptmap);

  g_mutex_clear (&priv->send_lock);
  g_cond_clear (&priv->send_cond);

  if (priv->block_early_rtcp_probe != 0) {
    gst_pad_remove_probe
        (priv->block_early_rtcp_pad, priv->block_early_rtcp_probe);
    gst_object_unref (priv->block_early_rtcp_pad);
  }

  if (priv->block_early_rtcp_probe_ipv6 != 0) {
    gst_pad_remove_probe
        (priv->block_early_rtcp_pad_ipv6, priv->block_early_rtcp_probe_ipv6);
    gst_object_unref (priv->block_early_rtcp_pad_ipv6);
  }

  G_OBJECT_CLASS (gst_rtsp_stream_parent_class)->finalize (obj);
}

static void
gst_rtsp_stream_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPStream *stream = GST_RTSP_STREAM (object);

  switch (propid) {
    case PROP_CONTROL:
      g_value_take_string (value, gst_rtsp_stream_get_control (stream));
      break;
    case PROP_PROFILES:
      g_value_set_flags (value, gst_rtsp_stream_get_profiles (stream));
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, gst_rtsp_stream_get_protocols (stream));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_stream_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPStream *stream = GST_RTSP_STREAM (object);

  switch (propid) {
    case PROP_CONTROL:
      gst_rtsp_stream_set_control (stream, g_value_get_string (value));
      break;
    case PROP_PROFILES:
      gst_rtsp_stream_set_profiles (stream, g_value_get_flags (value));
      break;
    case PROP_PROTOCOLS:
      gst_rtsp_stream_set_protocols (stream, g_value_get_flags (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_stream_new:
 * @idx: an index
 * @pad: a #GstPad
 * @payloader: a #GstElement
 *
 * Create a new media stream with index @idx that handles RTP data on
 * @pad and has a payloader element @payloader if @pad is a source pad
 * or a depayloader element @payloader if @pad is a sink pad.
 *
 * Returns: (transfer full): a new #GstRTSPStream
 */
GstRTSPStream *
gst_rtsp_stream_new (guint idx, GstElement * payloader, GstPad * pad)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPStream *stream;

  g_return_val_if_fail (GST_IS_ELEMENT (payloader), NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  stream = g_object_new (GST_TYPE_RTSP_STREAM, NULL);
  priv = stream->priv;
  priv->idx = idx;
  priv->payloader = gst_object_ref (payloader);
  if (GST_PAD_IS_SRC (pad))
    priv->srcpad = gst_object_ref (pad);
  else
    priv->sinkpad = gst_object_ref (pad);

  return stream;
}

/**
 * gst_rtsp_stream_get_index:
 * @stream: a #GstRTSPStream
 *
 * Get the stream index.
 *
 * Return: the stream index.
 */
guint
gst_rtsp_stream_get_index (GstRTSPStream * stream)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), -1);

  return stream->priv->idx;
}

/**
 * gst_rtsp_stream_get_pt:
 * @stream: a #GstRTSPStream
 *
 * Get the stream payload type.
 *
 * Return: the stream payload type.
 */
guint
gst_rtsp_stream_get_pt (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  guint pt;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), -1);

  priv = stream->priv;

  g_object_get (G_OBJECT (priv->payloader), "pt", &pt, NULL);

  return pt;
}

/**
 * gst_rtsp_stream_get_srcpad:
 * @stream: a #GstRTSPStream
 *
 * Get the srcpad associated with @stream.
 *
 * Returns: (transfer full) (nullable): the srcpad. Unref after usage.
 */
GstPad *
gst_rtsp_stream_get_srcpad (GstRTSPStream * stream)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  if (!stream->priv->srcpad)
    return NULL;

  return gst_object_ref (stream->priv->srcpad);
}

/**
 * gst_rtsp_stream_get_sinkpad:
 * @stream: a #GstRTSPStream
 *
 * Get the sinkpad associated with @stream.
 *
 * Returns: (transfer full) (nullable): the sinkpad. Unref after usage.
 */
GstPad *
gst_rtsp_stream_get_sinkpad (GstRTSPStream * stream)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  if (!stream->priv->sinkpad)
    return NULL;

  return gst_object_ref (stream->priv->sinkpad);
}

/**
 * gst_rtsp_stream_get_control:
 * @stream: a #GstRTSPStream
 *
 * Get the control string to identify this stream.
 *
 * Returns: (transfer full) (nullable): the control string. g_free() after usage.
 */
gchar *
gst_rtsp_stream_get_control (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if ((result = g_strdup (priv->control)) == NULL)
    result = g_strdup_printf ("stream=%u", priv->idx);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_stream_set_control:
 * @stream: a #GstRTSPStream
 * @control: (nullable): a control string
 *
 * Set the control string in @stream.
 */
void
gst_rtsp_stream_set_control (GstRTSPStream * stream, const gchar * control)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  g_free (priv->control);
  priv->control = g_strdup (control);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_has_control:
 * @stream: a #GstRTSPStream
 * @control: (nullable): a control string
 *
 * Check if @stream has the control string @control.
 *
 * Returns: %TRUE is @stream has @control as the control string
 */
gboolean
gst_rtsp_stream_has_control (GstRTSPStream * stream, const gchar * control)
{
  GstRTSPStreamPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if (priv->control)
    res = (g_strcmp0 (priv->control, control) == 0);
  else {
    guint streamid;

    if (sscanf (control, "stream=%u", &streamid) > 0)
      res = (streamid == priv->idx);
    else
      res = FALSE;
  }
  g_mutex_unlock (&priv->lock);

  return res;
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
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  GST_LOG_OBJECT (stream, "set MTU %u", mtu);

  g_object_set (G_OBJECT (priv->payloader), "mtu", mtu, NULL);
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
  GstRTSPStreamPrivate *priv;
  guint mtu;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), 0);

  priv = stream->priv;

  g_object_get (G_OBJECT (priv->payloader), "mtu", &mtu, NULL);

  return mtu;
}

/* Update the dscp qos property on the udp sinks */
static void
update_dscp_qos (GstRTSPStream * stream, GstElement ** udpsink)
{
  GstRTSPStreamPrivate *priv;

  priv = stream->priv;

  if (*udpsink) {
    g_object_set (G_OBJECT (*udpsink), "qos-dscp", priv->dscp_qos, NULL);
  }
}

/**
 * gst_rtsp_stream_set_dscp_qos:
 * @stream: a #GstRTSPStream
 * @dscp_qos: a new dscp qos value (0-63, or -1 to disable)
 *
 * Configure the dscp qos of the outgoing sockets to @dscp_qos.
 */
void
gst_rtsp_stream_set_dscp_qos (GstRTSPStream * stream, gint dscp_qos)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  GST_LOG_OBJECT (stream, "set DSCP QoS %d", dscp_qos);

  if (dscp_qos < -1 || dscp_qos > 63) {
    GST_WARNING_OBJECT (stream, "trying to set illegal dscp qos %d", dscp_qos);
    return;
  }

  priv->dscp_qos = dscp_qos;

  update_dscp_qos (stream, priv->udpsink);
}

/**
 * gst_rtsp_stream_get_dscp_qos:
 * @stream: a #GstRTSPStream
 *
 * Get the configured DSCP QoS in of the outgoing sockets.
 *
 * Returns: the DSCP QoS value of the outgoing sockets, or -1 if disbled.
 */
gint
gst_rtsp_stream_get_dscp_qos (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), -1);

  priv = stream->priv;

  return priv->dscp_qos;
}

/**
 * gst_rtsp_stream_is_transport_supported:
 * @stream: a #GstRTSPStream
 * @transport: (transfer none): a #GstRTSPTransport
 *
 * Check if @transport can be handled by stream
 *
 * Returns: %TRUE if @transport can be handled by @stream.
 */
gboolean
gst_rtsp_stream_is_transport_supported (GstRTSPStream * stream,
    GstRTSPTransport * transport)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (transport != NULL, FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if (transport->trans != GST_RTSP_TRANS_RTP)
    goto unsupported_transmode;

  if (!(transport->profile & priv->profiles))
    goto unsupported_profile;

  if (!(transport->lower_transport & priv->allowed_protocols))
    goto unsupported_ltrans;

  g_mutex_unlock (&priv->lock);

  return TRUE;

  /* ERRORS */
unsupported_transmode:
  {
    GST_DEBUG ("unsupported transport mode %d", transport->trans);
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
unsupported_profile:
  {
    GST_DEBUG ("unsupported profile %d", transport->profile);
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
unsupported_ltrans:
  {
    GST_DEBUG ("unsupported lower transport %d", transport->lower_transport);
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_set_profiles:
 * @stream: a #GstRTSPStream
 * @profiles: the new profiles
 *
 * Configure the allowed profiles for @stream.
 */
void
gst_rtsp_stream_set_profiles (GstRTSPStream * stream, GstRTSPProfile profiles)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  priv->profiles = profiles;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_get_profiles:
 * @stream: a #GstRTSPStream
 *
 * Get the allowed profiles of @stream.
 *
 * Returns: a #GstRTSPProfile
 */
GstRTSPProfile
gst_rtsp_stream_get_profiles (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPProfile res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), GST_RTSP_PROFILE_UNKNOWN);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  res = priv->profiles;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_set_protocols:
 * @stream: a #GstRTSPStream
 * @protocols: the new flags
 *
 * Configure the allowed lower transport for @stream.
 */
void
gst_rtsp_stream_set_protocols (GstRTSPStream * stream,
    GstRTSPLowerTrans protocols)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  priv->allowed_protocols = protocols;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_get_protocols:
 * @stream: a #GstRTSPStream
 *
 * Get the allowed protocols of @stream.
 *
 * Returns: a #GstRTSPLowerTrans
 */
GstRTSPLowerTrans
gst_rtsp_stream_get_protocols (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPLowerTrans res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream),
      GST_RTSP_LOWER_TRANS_UNKNOWN);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  res = priv->allowed_protocols;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_set_address_pool:
 * @stream: a #GstRTSPStream
 * @pool: (transfer none) (nullable): a #GstRTSPAddressPool
 *
 * configure @pool to be used as the address pool of @stream.
 */
void
gst_rtsp_stream_set_address_pool (GstRTSPStream * stream,
    GstRTSPAddressPool * pool)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPAddressPool *old;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  GST_LOG_OBJECT (stream, "set address pool %p", pool);

  g_mutex_lock (&priv->lock);
  if ((old = priv->pool) != pool)
    priv->pool = pool ? g_object_ref (pool) : NULL;
  else
    old = NULL;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_stream_get_address_pool:
 * @stream: a #GstRTSPStream
 *
 * Get the #GstRTSPAddressPool used as the address pool of @stream.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPAddressPool of @stream.
 * g_object_unref() after usage.
 */
GstRTSPAddressPool *
gst_rtsp_stream_get_address_pool (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPAddressPool *result;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->pool))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_stream_set_multicast_iface:
 * @stream: a #GstRTSPStream
 * @multicast_iface: (transfer none) (nullable): a multicast interface name
 *
 * configure @multicast_iface to be used for @stream.
 */
void
gst_rtsp_stream_set_multicast_iface (GstRTSPStream * stream,
    const gchar * multicast_iface)
{
  GstRTSPStreamPrivate *priv;
  gchar *old;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  GST_LOG_OBJECT (stream, "set multicast iface %s",
      GST_STR_NULL (multicast_iface));

  g_mutex_lock (&priv->lock);
  if ((old = priv->multicast_iface) != multicast_iface)
    priv->multicast_iface = multicast_iface ? g_strdup (multicast_iface) : NULL;
  else
    old = NULL;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_free (old);
}

/**
 * gst_rtsp_stream_get_multicast_iface:
 * @stream: a #GstRTSPStream
 *
 * Get the multicast interface used for @stream.
 *
 * Returns: (transfer full) (nullable): the multicast interface for @stream.
 * g_free() after usage.
 */
gchar *
gst_rtsp_stream_get_multicast_iface (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->multicast_iface))
    result = g_strdup (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_stream_get_multicast_address:
 * @stream: a #GstRTSPStream
 * @family: the #GSocketFamily
 *
 * Get the multicast address of @stream for @family. The original
 * #GstRTSPAddress is cached and copy is returned, so freeing the return value
 * won't release the address from the pool.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPAddress of @stream
 * or %NULL when no address could be allocated. gst_rtsp_address_free()
 * after usage.
 */
GstRTSPAddress *
gst_rtsp_stream_get_multicast_address (GstRTSPStream * stream,
    GSocketFamily family)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPAddress *result;
  GstRTSPAddress **addrp;
  GstRTSPAddressFlags flags;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&stream->priv->lock);

  if (family == G_SOCKET_FAMILY_IPV6) {
    flags = GST_RTSP_ADDRESS_FLAG_IPV6;
    addrp = &priv->mcast_addr_v6;
  } else {
    flags = GST_RTSP_ADDRESS_FLAG_IPV4;
    addrp = &priv->mcast_addr_v4;
  }

  if (*addrp == NULL) {
    if (priv->pool == NULL)
      goto no_pool;

    flags |= GST_RTSP_ADDRESS_FLAG_EVEN_PORT | GST_RTSP_ADDRESS_FLAG_MULTICAST;

    *addrp = gst_rtsp_address_pool_acquire_address (priv->pool, flags, 2);
    if (*addrp == NULL)
      goto no_address;

    /* FIXME: Also reserve the same port with unicast ANY address, since that's
     * where we are going to bind our socket. Probably loop until we find a port
     * available in both mcast and unicast pools. Maybe GstRTSPAddressPool
     * should do it for us when both GST_RTSP_ADDRESS_FLAG_MULTICAST and
     * GST_RTSP_ADDRESS_FLAG_UNICAST are givent. */
  }
  result = gst_rtsp_address_copy (*addrp);

  g_mutex_unlock (&stream->priv->lock);

  return result;

  /* ERRORS */
no_pool:
  {
    GST_ERROR_OBJECT (stream, "no address pool specified");
    g_mutex_unlock (&stream->priv->lock);
    return NULL;
  }
no_address:
  {
    GST_ERROR_OBJECT (stream, "failed to acquire address from pool");
    g_mutex_unlock (&stream->priv->lock);
    return NULL;
  }
}

/**
 * gst_rtsp_stream_reserve_address:
 * @stream: a #GstRTSPStream
 * @address: an address
 * @port: a port
 * @n_ports: n_ports
 * @ttl: a TTL
 *
 * Reserve @address and @port as the address and port of @stream. The original
 * #GstRTSPAddress is cached and copy is returned, so freeing the return value
 * won't release the address from the pool.
 *
 * Returns: (nullable): the #GstRTSPAddress of @stream or %NULL when
 * the address could not be reserved. gst_rtsp_address_free() after
 * usage.
 */
GstRTSPAddress *
gst_rtsp_stream_reserve_address (GstRTSPStream * stream,
    const gchar * address, guint port, guint n_ports, guint ttl)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPAddress *result;
  GInetAddress *addr;
  GSocketFamily family;
  GstRTSPAddress **addrp;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (address != NULL, NULL);
  g_return_val_if_fail (port > 0, NULL);
  g_return_val_if_fail (n_ports > 0, NULL);
  g_return_val_if_fail (ttl > 0, NULL);

  priv = stream->priv;

  addr = g_inet_address_new_from_string (address);
  if (!addr) {
    GST_ERROR ("failed to get inet addr from %s", address);
    family = G_SOCKET_FAMILY_IPV4;
  } else {
    family = g_inet_address_get_family (addr);
    g_object_unref (addr);
  }

  if (family == G_SOCKET_FAMILY_IPV6)
    addrp = &priv->mcast_addr_v6;
  else
    addrp = &priv->mcast_addr_v4;

  g_mutex_lock (&priv->lock);
  if (*addrp == NULL) {
    GstRTSPAddressPoolResult res;

    if (priv->pool == NULL)
      goto no_pool;

    res = gst_rtsp_address_pool_reserve_address (priv->pool, address,
        port, n_ports, ttl, addrp);
    if (res != GST_RTSP_ADDRESS_POOL_OK)
      goto no_address;

    /* FIXME: Also reserve the same port with unicast ANY address, since that's
     * where we are going to bind our socket. */
  } else {
    if (g_ascii_strcasecmp ((*addrp)->address, address) ||
        (*addrp)->port != port || (*addrp)->n_ports != n_ports ||
        (*addrp)->ttl != ttl)
      goto different_address;
  }
  result = gst_rtsp_address_copy (*addrp);
  g_mutex_unlock (&priv->lock);

  return result;

  /* ERRORS */
no_pool:
  {
    GST_ERROR_OBJECT (stream, "no address pool specified");
    g_mutex_unlock (&priv->lock);
    return NULL;
  }
no_address:
  {
    GST_ERROR_OBJECT (stream, "failed to acquire address %s from pool",
        address);
    g_mutex_unlock (&priv->lock);
    return NULL;
  }
different_address:
  {
    GST_ERROR_OBJECT (stream,
        "address %s is not the same as %s that was already reserved",
        address, (*addrp)->address);
    g_mutex_unlock (&priv->lock);
    return NULL;
  }
}

/* must be called with lock */
static void
set_socket_for_udpsink (GstElement * udpsink, GSocket * socket,
    GSocketFamily family)
{
  const gchar *multisink_socket;

  if (family == G_SOCKET_FAMILY_IPV6)
    multisink_socket = "socket-v6";
  else
    multisink_socket = "socket";

  g_object_set (G_OBJECT (udpsink), multisink_socket, socket, NULL);
}

/* must be called with lock */
static void
set_multicast_socket_for_udpsink (GstElement * udpsink, GSocket * socket,
    GSocketFamily family, const gchar * multicast_iface,
    const gchar * addr_str, gint port, gint mcast_ttl)
{
  set_socket_for_udpsink (udpsink, socket, family);

  if (multicast_iface) {
    GST_INFO ("setting multicast-iface %s", multicast_iface);
    g_object_set (G_OBJECT (udpsink), "multicast-iface", multicast_iface, NULL);
  }

  if (mcast_ttl > 0) {
    GST_INFO ("setting ttl-mc %d", mcast_ttl);
    g_object_set (G_OBJECT (udpsink), "ttl-mc", mcast_ttl, NULL);
  }
}


/* must be called with lock */
static void
set_unicast_socket_for_udpsink (GstElement * udpsink, GSocket * socket,
    GSocketFamily family)
{
  set_socket_for_udpsink (udpsink, socket, family);
}

static guint16
get_port_from_socket (GSocket * socket)
{
  guint16 port;
  GSocketAddress *sockaddr;
  GError *err;

  GST_DEBUG ("socket: %p", socket);
  sockaddr = g_socket_get_local_address (socket, &err);
  if (sockaddr == NULL || !G_IS_INET_SOCKET_ADDRESS (sockaddr)) {
    g_clear_object (&sockaddr);
    GST_ERROR ("failed to get sockaddr: %s", err->message);
    g_error_free (err);
    return 0;
  }

  port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (sockaddr));
  g_object_unref (sockaddr);

  return port;
}


static gboolean
create_and_configure_udpsink (GstRTSPStream * stream, GstElement ** udpsink,
    GSocket * socket_v4, GSocket * socket_v6, gboolean multicast,
    gboolean is_rtp, gint mcast_ttl)
{
  GstRTSPStreamPrivate *priv = stream->priv;

  *udpsink = gst_element_factory_make ("multiudpsink", NULL);

  if (!*udpsink)
    goto no_udp_protocol;

  /* configure sinks */

  g_object_set (G_OBJECT (*udpsink), "close-socket", FALSE, NULL);

  g_object_set (G_OBJECT (*udpsink), "send-duplicates", FALSE, NULL);

  if (is_rtp)
    g_object_set (G_OBJECT (*udpsink), "buffer-size", priv->buffer_size, NULL);
  else
    g_object_set (G_OBJECT (*udpsink), "sync", FALSE, NULL);

  /* Needs to be async for RECORD streams, otherwise we will never go to
   * PLAYING because the sinks will wait for data while the udpsrc can't
   * provide data with timestamps in PAUSED. */
  if (!is_rtp || priv->sinkpad)
    g_object_set (G_OBJECT (*udpsink), "async", FALSE, NULL);

  if (multicast) {
    /* join multicast group when adding clients, so we'll start receiving from it.
     * We cannot rely on the udpsrc to join the group since its socket is always a
     * local unicast one. */
    g_object_set (G_OBJECT (*udpsink), "auto-multicast", TRUE, NULL);

    g_object_set (G_OBJECT (*udpsink), "loop", FALSE, NULL);
  }

  /* update the dscp qos field in the sinks */
  update_dscp_qos (stream, udpsink);

  if (priv->server_addr_v4) {
    GST_DEBUG_OBJECT (stream, "udp IPv4, configure udpsinks");
    set_unicast_socket_for_udpsink (*udpsink, socket_v4, G_SOCKET_FAMILY_IPV4);
  }

  if (priv->server_addr_v6) {
    GST_DEBUG_OBJECT (stream, "udp IPv6, configure udpsinks");
    set_unicast_socket_for_udpsink (*udpsink, socket_v6, G_SOCKET_FAMILY_IPV6);
  }

  if (multicast) {
    gint port;
    if (priv->mcast_addr_v4) {
      GST_DEBUG_OBJECT (stream, "mcast IPv4, configure udpsinks");
      port = get_port_from_socket (socket_v4);
      if (!port)
        goto get_port_failed;
      set_multicast_socket_for_udpsink (*udpsink, socket_v4,
          G_SOCKET_FAMILY_IPV4, priv->multicast_iface,
          priv->mcast_addr_v4->address, port, mcast_ttl);
    }

    if (priv->mcast_addr_v6) {
      GST_DEBUG_OBJECT (stream, "mcast IPv6, configure udpsinks");
      port = get_port_from_socket (socket_v6);
      if (!port)
        goto get_port_failed;
      set_multicast_socket_for_udpsink (*udpsink, socket_v6,
          G_SOCKET_FAMILY_IPV6, priv->multicast_iface,
          priv->mcast_addr_v6->address, port, mcast_ttl);
    }

  }

  return TRUE;

  /* ERRORS */
no_udp_protocol:
  {
    GST_ERROR_OBJECT (stream, "failed to create udpsink element");
    return FALSE;
  }
get_port_failed:
  {
    GST_ERROR_OBJECT (stream, "failed to get udp port");
    return FALSE;
  }
}

/* must be called with lock */
static gboolean
create_and_configure_udpsource (GstElement ** udpsrc, GSocket * socket)
{
  GstStateChangeReturn ret;

  g_assert (socket != NULL);

  *udpsrc = gst_element_factory_make ("udpsrc", NULL);
  if (*udpsrc == NULL)
    goto error;

  g_object_set (G_OBJECT (*udpsrc), "socket", socket, NULL);

  /* The udpsrc cannot do the join because its socket is always a local unicast
   * one. The udpsink sharing the same socket will do it for us. */
  g_object_set (G_OBJECT (*udpsrc), "auto-multicast", FALSE, NULL);

  g_object_set (G_OBJECT (*udpsrc), "loop", FALSE, NULL);

  g_object_set (G_OBJECT (*udpsrc), "close-socket", FALSE, NULL);

  ret = gst_element_set_state (*udpsrc, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto error;

  return TRUE;

  /* ERRORS */
error:
  {
    if (*udpsrc) {
      gst_element_set_state (*udpsrc, GST_STATE_NULL);
      g_clear_object (udpsrc);
    }
    return FALSE;
  }
}

static gboolean
alloc_ports_one_family (GstRTSPStream * stream, GSocketFamily family,
    GSocket * socket_out[2], GstRTSPAddress ** server_addr_out,
    gboolean multicast, GstRTSPTransport * ct, gboolean use_transport_settings)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GSocket *rtp_socket = NULL;
  GSocket *rtcp_socket = NULL;
  gint tmp_rtp, tmp_rtcp;
  guint count;
  GList *rejected_addresses = NULL;
  GstRTSPAddress *addr = NULL;
  GInetAddress *inetaddr = NULL;
  GSocketAddress *rtp_sockaddr = NULL;
  GSocketAddress *rtcp_sockaddr = NULL;
  GstRTSPAddressPool *pool;
  gboolean transport_settings_defined = FALSE;

  pool = priv->pool;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;
  tmp_rtcp = 0;

  if (use_transport_settings) {
    if (!multicast)
      goto no_mcast;

    if (ct == NULL)
      goto no_transport;

    /* multicast and transport specific case */
    if (ct->destination != NULL) {
      tmp_rtp = ct->port.min;
      tmp_rtcp = ct->port.max;

      /* check if the provided address is a multicast address */
      inetaddr = g_inet_address_new_from_string (ct->destination);
      if (inetaddr == NULL)
        goto destination_error;
      if (!g_inet_address_get_is_multicast (inetaddr))
        goto destination_no_mcast;


      if (!priv->bind_mcast_address) {
        g_clear_object (&inetaddr);
        inetaddr = g_inet_address_new_any (family);
      }

      GST_DEBUG_OBJECT (stream, "use transport settings");
      transport_settings_defined = TRUE;
    }
  }

  if (priv->enable_rtcp) {
    rtcp_socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
        G_SOCKET_PROTOCOL_UDP, NULL);
    if (!rtcp_socket)
      goto no_udp_protocol;
    g_socket_set_multicast_loopback (rtcp_socket, FALSE);
  }

  /* try to allocate UDP ports, the RTP port should be an even
   * number and the RTCP port (if enabled) should be the next (uneven) port */
again:

  if (rtp_socket == NULL) {
    rtp_socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
        G_SOCKET_PROTOCOL_UDP, NULL);
    if (!rtp_socket)
      goto no_udp_protocol;
    g_socket_set_multicast_loopback (rtp_socket, FALSE);
  }

  if (!transport_settings_defined) {
    if ((pool && gst_rtsp_address_pool_has_unicast_addresses (pool))
        || multicast) {
      GstRTSPAddressFlags flags;

      if (addr) {
        g_assert (*server_addr_out == NULL);
        rejected_addresses = g_list_prepend (rejected_addresses, addr);
      }

      if (!pool)
        goto no_pool;

      flags = GST_RTSP_ADDRESS_FLAG_EVEN_PORT;
      if (multicast)
        flags |= GST_RTSP_ADDRESS_FLAG_MULTICAST;
      else
        flags |= GST_RTSP_ADDRESS_FLAG_UNICAST;

      if (family == G_SOCKET_FAMILY_IPV6)
        flags |= GST_RTSP_ADDRESS_FLAG_IPV6;
      else
        flags |= GST_RTSP_ADDRESS_FLAG_IPV4;

      if (*server_addr_out)
        addr = *server_addr_out;
      else
        addr = gst_rtsp_address_pool_acquire_address (pool, flags,
            priv->enable_rtcp ? 2 : 1);

      if (addr == NULL)
        goto no_address;

      tmp_rtp = addr->port;

      g_clear_object (&inetaddr);
      /* FIXME: Does it really work with the IP_MULTICAST_ALL socket option and
       * socket control message set in udpsrc? */
      if (priv->bind_mcast_address || !multicast)
        inetaddr = g_inet_address_new_from_string (addr->address);
      else
        inetaddr = g_inet_address_new_any (family);
    } else {
      if (tmp_rtp != 0) {
        tmp_rtp += 2;
        if (++count > 20)
          goto no_ports;
      }

      if (inetaddr == NULL)
        inetaddr = g_inet_address_new_any (family);
    }
  }

  rtp_sockaddr = g_inet_socket_address_new (inetaddr, tmp_rtp);
  if (!g_socket_bind (rtp_socket, rtp_sockaddr, FALSE, NULL)) {
    GST_DEBUG_OBJECT (stream, "rtp bind() failed, will try again");
    g_object_unref (rtp_sockaddr);
    if (transport_settings_defined) {
      goto transport_settings_error;
    } else if (*server_addr_out && ((pool
                && gst_rtsp_address_pool_has_unicast_addresses (pool))
            || multicast)) {
      goto no_address;
    } else {
      goto again;
    }
  }
  g_object_unref (rtp_sockaddr);

  rtp_sockaddr = g_socket_get_local_address (rtp_socket, NULL);
  if (rtp_sockaddr == NULL || !G_IS_INET_SOCKET_ADDRESS (rtp_sockaddr)) {
    g_clear_object (&rtp_sockaddr);
    goto socket_error;
  }

  if (!transport_settings_defined) {
    tmp_rtp =
        g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (rtp_sockaddr));

    /* check if port is even. RFC 3550 encorages the use of an even/odd port
     * pair, however it's not a strict requirement so this check is not done
     * for the client selected ports. */
    if ((tmp_rtp & 1) != 0) {
      /* port not even, close and allocate another */
      tmp_rtp++;
      g_object_unref (rtp_sockaddr);
      g_clear_object (&rtp_socket);
      goto again;
    }
  }
  g_object_unref (rtp_sockaddr);

  /* set port */
  if (priv->enable_rtcp) {
    tmp_rtcp = tmp_rtp + 1;

    rtcp_sockaddr = g_inet_socket_address_new (inetaddr, tmp_rtcp);
    if (!g_socket_bind (rtcp_socket, rtcp_sockaddr, FALSE, NULL)) {
      GST_DEBUG_OBJECT (stream, "rctp bind() failed, will try again");
      g_object_unref (rtcp_sockaddr);
      g_clear_object (&rtp_socket);
      if (transport_settings_defined)
        goto transport_settings_error;
      goto again;
    }
    g_object_unref (rtcp_sockaddr);
  }

  if (!addr) {
    addr = g_new0 (GstRTSPAddress, 1);
    addr->port = tmp_rtp;
    addr->n_ports = 2;
    if (transport_settings_defined)
      addr->address = g_strdup (ct->destination);
    else
      addr->address = g_inet_address_to_string (inetaddr);
    addr->ttl = ct->ttl;
  }

  g_clear_object (&inetaddr);

  if (multicast && (ct->ttl > 0) && (ct->ttl <= priv->max_mcast_ttl)) {
    GST_DEBUG ("setting mcast ttl to %d", ct->ttl);
    g_socket_set_multicast_ttl (rtp_socket, ct->ttl);
    if (rtcp_socket)
      g_socket_set_multicast_ttl (rtcp_socket, ct->ttl);
  }

  socket_out[0] = rtp_socket;
  socket_out[1] = rtcp_socket;
  *server_addr_out = addr;

  if (priv->enable_rtcp) {
    GST_DEBUG_OBJECT (stream, "allocated address: %s and ports: %d, %d",
        addr->address, tmp_rtp, tmp_rtcp);
  } else {
    GST_DEBUG_OBJECT (stream, "allocated address: %s and port: %d",
        addr->address, tmp_rtp);
  }

  g_list_free_full (rejected_addresses, (GDestroyNotify) gst_rtsp_address_free);

  return TRUE;

  /* ERRORS */
no_mcast:
  {
    GST_ERROR_OBJECT (stream, "failed to allocate UDP ports: wrong transport");
    goto cleanup;
  }
no_transport:
  {
    GST_ERROR_OBJECT (stream, "failed to allocate UDP ports: no transport");
    goto cleanup;
  }
destination_error:
  {
    GST_ERROR_OBJECT (stream,
        "failed to allocate UDP ports: destination error");
    goto cleanup;
  }
destination_no_mcast:
  {
    GST_ERROR_OBJECT (stream,
        "failed to allocate UDP ports: destination not multicast address");
    goto cleanup;
  }
no_udp_protocol:
  {
    GST_WARNING_OBJECT (stream, "failed to allocate UDP ports: protocol error");
    goto cleanup;
  }
no_pool:
  {
    GST_WARNING_OBJECT (stream,
        "failed to allocate UDP ports: no address pool specified");
    goto cleanup;
  }
no_address:
  {
    GST_WARNING_OBJECT (stream, "failed to acquire address from pool");
    goto cleanup;
  }
no_ports:
  {
    GST_WARNING_OBJECT (stream, "failed to allocate UDP ports: no ports");
    goto cleanup;
  }
transport_settings_error:
  {
    GST_ERROR_OBJECT (stream,
        "failed to allocate UDP ports with requested transport settings");
    goto cleanup;
  }
socket_error:
  {
    GST_WARNING_OBJECT (stream, "failed to allocate UDP ports: socket error");
    goto cleanup;
  }
cleanup:
  {
    if (inetaddr)
      g_object_unref (inetaddr);
    g_list_free_full (rejected_addresses,
        (GDestroyNotify) gst_rtsp_address_free);
    if (addr)
      gst_rtsp_address_free (addr);
    if (rtp_socket)
      g_object_unref (rtp_socket);
    if (rtcp_socket)
      g_object_unref (rtcp_socket);
    return FALSE;
  }
}

/* must be called with lock */
static gboolean
add_mcast_client_addr (GstRTSPStream * stream, const gchar * destination,
    guint rtp_port, guint rtcp_port)
{
  GstRTSPStreamPrivate *priv;
  GList *walk;
  UdpClientAddrInfo *client;
  GInetAddress *inet;

  priv = stream->priv;

  if (destination == NULL)
    return FALSE;

  inet = g_inet_address_new_from_string (destination);
  if (inet == NULL)
    goto invalid_address;

  if (!g_inet_address_get_is_multicast (inet)) {
    g_object_unref (inet);
    goto invalid_address;
  }
  g_object_unref (inet);

  for (walk = priv->mcast_clients; walk; walk = g_list_next (walk)) {
    UdpClientAddrInfo *cli = walk->data;

    if ((g_strcmp0 (cli->address, destination) == 0) &&
        (cli->rtp_port == rtp_port)) {
      GST_DEBUG ("requested destination already exists: %s:%u-%u",
          destination, rtp_port, rtcp_port);
      cli->add_count++;
      return TRUE;
    }
  }

  client = g_new0 (UdpClientAddrInfo, 1);
  client->address = g_strdup (destination);
  client->rtp_port = rtp_port;
  client->add_count = 1;
  priv->mcast_clients = g_list_prepend (priv->mcast_clients, client);

  GST_DEBUG ("added mcast client %s:%u-%u", destination, rtp_port, rtcp_port);

  return TRUE;

invalid_address:
  {
    GST_WARNING_OBJECT (stream, "Multicast address is invalid: %s",
        destination);
    return FALSE;
  }
}

/* must be called with lock */
static gboolean
remove_mcast_client_addr (GstRTSPStream * stream, const gchar * destination,
    guint rtp_port, guint rtcp_port)
{
  GstRTSPStreamPrivate *priv;
  GList *walk;

  priv = stream->priv;

  if (destination == NULL)
    goto no_destination;

  for (walk = priv->mcast_clients; walk; walk = g_list_next (walk)) {
    UdpClientAddrInfo *cli = walk->data;

    if ((g_strcmp0 (cli->address, destination) == 0) &&
        (cli->rtp_port == rtp_port)) {
      cli->add_count--;

      if (!cli->add_count) {
        priv->mcast_clients = g_list_remove (priv->mcast_clients, cli);
        free_mcast_client (cli);
      }
      return TRUE;
    }
  }

  GST_WARNING_OBJECT (stream, "Address not found");
  return FALSE;

no_destination:
  {
    GST_WARNING_OBJECT (stream, "No destination has been provided");
    return FALSE;
  }
}


/**
 * gst_rtsp_stream_allocate_udp_sockets:
 * @stream: a #GstRTSPStream
 * @family: protocol family
 * @transport: transport method
 * @use_client_settings: Whether to use client settings or not
 *
 * Allocates RTP and RTCP ports.
 *
 * Returns: %TRUE if the RTP and RTCP sockets have been succeccully allocated.
 */
gboolean
gst_rtsp_stream_allocate_udp_sockets (GstRTSPStream * stream,
    GSocketFamily family, GstRTSPTransport * ct,
    gboolean use_transport_settings)
{
  GstRTSPStreamPrivate *priv;
  gboolean ret = FALSE;
  GstRTSPLowerTrans transport;
  gboolean allocated = FALSE;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (ct != NULL, FALSE);
  priv = stream->priv;

  transport = ct->lower_transport;

  g_mutex_lock (&priv->lock);

  if (transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    if (family == G_SOCKET_FAMILY_IPV4 && priv->mcast_socket_v4[0])
      allocated = TRUE;
    else if (family == G_SOCKET_FAMILY_IPV6 && priv->mcast_socket_v6[0])
      allocated = TRUE;
  } else if (transport == GST_RTSP_LOWER_TRANS_UDP) {
    if (family == G_SOCKET_FAMILY_IPV4 && priv->socket_v4[0])
      allocated = TRUE;
    else if (family == G_SOCKET_FAMILY_IPV6 && priv->socket_v6[0])
      allocated = TRUE;
  }

  if (allocated) {
    GST_DEBUG_OBJECT (stream, "Allocated already");
    g_mutex_unlock (&priv->lock);
    return TRUE;
  }

  if (family == G_SOCKET_FAMILY_IPV4) {
    /* IPv4 */
    if (transport == GST_RTSP_LOWER_TRANS_UDP) {
      /* UDP unicast */
      GST_DEBUG_OBJECT (stream, "GST_RTSP_LOWER_TRANS_UDP, ipv4");
      ret = alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV4,
          priv->socket_v4, &priv->server_addr_v4, FALSE, ct, FALSE);
    } else {
      /* multicast */
      GST_DEBUG_OBJECT (stream, "GST_RTSP_LOWER_TRANS_MCAST_UDP, ipv4");
      ret = alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV4,
          priv->mcast_socket_v4, &priv->mcast_addr_v4, TRUE, ct,
          use_transport_settings);
    }
  } else {
    /* IPv6 */
    if (transport == GST_RTSP_LOWER_TRANS_UDP) {
      /* unicast */
      GST_DEBUG_OBJECT (stream, "GST_RTSP_LOWER_TRANS_UDP, ipv6");
      ret = alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV6,
          priv->socket_v6, &priv->server_addr_v6, FALSE, ct, FALSE);

    } else {
      /* multicast */
      GST_DEBUG_OBJECT (stream, "GST_RTSP_LOWER_TRANS_MCAST_UDP, ipv6");
      ret = alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV6,
          priv->mcast_socket_v6, &priv->mcast_addr_v6, TRUE, ct,
          use_transport_settings);
    }
  }
  g_mutex_unlock (&priv->lock);

  return ret;
}

/**
 * gst_rtsp_stream_set_client_side:
 * @stream: a #GstRTSPStream
 * @client_side: TRUE if this #GstRTSPStream is running on the 'client' side of
 * an RTSP connection.
 *
 * Sets the #GstRTSPStream as a 'client side' stream - used for sending
 * streams to an RTSP server via RECORD. This has the practical effect
 * of changing which UDP port numbers are used when setting up the local
 * side of the stream sending to be either the 'server' or 'client' pair
 * of a configured UDP transport.
 */
void
gst_rtsp_stream_set_client_side (GstRTSPStream * stream, gboolean client_side)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));
  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  priv->client_side = client_side;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_is_client_side:
 * @stream: a #GstRTSPStream
 *
 * See gst_rtsp_stream_set_client_side()
 *
 * Returns: TRUE if this #GstRTSPStream is client-side.
 */
gboolean
gst_rtsp_stream_is_client_side (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  ret = priv->client_side;
  g_mutex_unlock (&priv->lock);

  return ret;
}

/**
 * gst_rtsp_stream_get_server_port:
 * @stream: a #GstRTSPStream
 * @server_port: (out): result server port
 * @family: the port family to get
 *
 * Fill @server_port with the port pair used by the server. This function can
 * only be called when @stream has been joined.
 */
void
gst_rtsp_stream_get_server_port (GstRTSPStream * stream,
    GstRTSPRange * server_port, GSocketFamily family)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));
  priv = stream->priv;
  g_return_if_fail (priv->joined_bin != NULL);

  if (server_port) {
    server_port->min = 0;
    server_port->max = 0;
  }

  g_mutex_lock (&priv->lock);
  if (family == G_SOCKET_FAMILY_IPV4) {
    if (server_port && priv->server_addr_v4) {
      server_port->min = priv->server_addr_v4->port;
      if (priv->enable_rtcp) {
        server_port->max =
            priv->server_addr_v4->port + priv->server_addr_v4->n_ports - 1;
      }
    }
  } else {
    if (server_port && priv->server_addr_v6) {
      server_port->min = priv->server_addr_v6->port;
      if (priv->enable_rtcp) {
        server_port->max =
            priv->server_addr_v6->port + priv->server_addr_v6->n_ports - 1;
      }
    }
  }
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_get_rtpsession:
 * @stream: a #GstRTSPStream
 *
 * Get the RTP session of this stream.
 *
 * Returns: (transfer full) (nullable): The RTP session of this stream. Unref after usage.
 */
GObject *
gst_rtsp_stream_get_rtpsession (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GObject *session;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if ((session = priv->session))
    g_object_ref (session);
  g_mutex_unlock (&priv->lock);

  return session;
}

/**
 * gst_rtsp_stream_get_srtp_encoder:
 * @stream: a #GstRTSPStream
 *
 * Get the SRTP encoder for this stream.
 *
 * Returns: (transfer full) (nullable): The SRTP encoder for this stream. Unref after usage.
 */
GstElement *
gst_rtsp_stream_get_srtp_encoder (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstElement *encoder;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if ((encoder = priv->srtpenc))
    g_object_ref (encoder);
  g_mutex_unlock (&priv->lock);

  return encoder;
}

/**
 * gst_rtsp_stream_get_ssrc:
 * @stream: a #GstRTSPStream
 * @ssrc: (out): result ssrc
 *
 * Get the SSRC used by the RTP session of this stream. This function can only
 * be called when @stream has been joined.
 */
void
gst_rtsp_stream_get_ssrc (GstRTSPStream * stream, guint * ssrc)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));
  priv = stream->priv;
  g_return_if_fail (priv->joined_bin != NULL);

  g_mutex_lock (&priv->lock);
  if (ssrc && priv->session)
    g_object_get (priv->session, "internal-ssrc", ssrc, NULL);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_set_retransmission_time:
 * @stream: a #GstRTSPStream
 * @time: a #GstClockTime
 *
 * Set the amount of time to store retransmission packets.
 */
void
gst_rtsp_stream_set_retransmission_time (GstRTSPStream * stream,
    GstClockTime time)
{
  GST_DEBUG_OBJECT (stream, "set retransmission time %" G_GUINT64_FORMAT, time);

  g_mutex_lock (&stream->priv->lock);
  stream->priv->rtx_time = time;
  if (stream->priv->rtxsend)
    g_object_set (stream->priv->rtxsend, "max-size-time",
        GST_TIME_AS_MSECONDS (time), NULL);
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_get_retransmission_time:
 * @stream: a #GstRTSPStream
 *
 * Get the amount of time to store retransmission data.
 *
 * Returns: the amount of time to store retransmission data.
 */
GstClockTime
gst_rtsp_stream_get_retransmission_time (GstRTSPStream * stream)
{
  GstClockTime ret;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), 0);

  g_mutex_lock (&stream->priv->lock);
  ret = stream->priv->rtx_time;
  g_mutex_unlock (&stream->priv->lock);

  return ret;
}

/**
 * gst_rtsp_stream_set_retransmission_pt:
 * @stream: a #GstRTSPStream
 * @rtx_pt: a #guint
 *
 * Set the payload type (pt) for retransmission of this stream.
 */
void
gst_rtsp_stream_set_retransmission_pt (GstRTSPStream * stream, guint rtx_pt)
{
  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  GST_DEBUG_OBJECT (stream, "set retransmission pt %u", rtx_pt);

  g_mutex_lock (&stream->priv->lock);
  stream->priv->rtx_pt = rtx_pt;
  if (stream->priv->rtxsend) {
    guint pt = gst_rtsp_stream_get_pt (stream);
    gchar *pt_s = g_strdup_printf ("%d", pt);
    GstStructure *rtx_pt_map = gst_structure_new ("application/x-rtp-pt-map",
        pt_s, G_TYPE_UINT, rtx_pt, NULL);
    g_object_set (stream->priv->rtxsend, "payload-type-map", rtx_pt_map, NULL);
    g_free (pt_s);
    gst_structure_free (rtx_pt_map);
  }
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_get_retransmission_pt:
 * @stream: a #GstRTSPStream
 *
 * Get the payload-type used for retransmission of this stream
 *
 * Returns: The retransmission PT.
 */
guint
gst_rtsp_stream_get_retransmission_pt (GstRTSPStream * stream)
{
  guint rtx_pt;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), 0);

  g_mutex_lock (&stream->priv->lock);
  rtx_pt = stream->priv->rtx_pt;
  g_mutex_unlock (&stream->priv->lock);

  return rtx_pt;
}

/**
 * gst_rtsp_stream_set_buffer_size:
 * @stream: a #GstRTSPStream
 * @size: the buffer size
 *
 * Set the size of the UDP transmission buffer (in bytes)
 * Needs to be set before the stream is joined to a bin.
 *
 * Since: 1.6
 */
void
gst_rtsp_stream_set_buffer_size (GstRTSPStream * stream, guint size)
{
  g_mutex_lock (&stream->priv->lock);
  stream->priv->buffer_size = size;
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_get_buffer_size:
 * @stream: a #GstRTSPStream
 *
 * Get the size of the UDP transmission buffer (in bytes)
 *
 * Returns: the size of the UDP TX buffer
 *
 * Since: 1.6
 */
guint
gst_rtsp_stream_get_buffer_size (GstRTSPStream * stream)
{
  guint buffer_size;

  g_mutex_lock (&stream->priv->lock);
  buffer_size = stream->priv->buffer_size;
  g_mutex_unlock (&stream->priv->lock);

  return buffer_size;
}

/**
 * gst_rtsp_stream_set_max_mcast_ttl:
 * @stream: a #GstRTSPStream
 * @ttl: the new multicast ttl value
 *
 * Set the maximum time-to-live value of outgoing multicast packets.
 *
 * Returns: %TRUE if the requested ttl has been set successfully.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_stream_set_max_mcast_ttl (GstRTSPStream * stream, guint ttl)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  g_mutex_lock (&stream->priv->lock);
  if (ttl == 0 || ttl > DEFAULT_MAX_MCAST_TTL) {
    GST_WARNING_OBJECT (stream, "The reqested mcast TTL value is not valid.");
    g_mutex_unlock (&stream->priv->lock);
    return FALSE;
  }
  stream->priv->max_mcast_ttl = ttl;
  g_mutex_unlock (&stream->priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_stream_get_max_mcast_ttl:
 * @stream: a #GstRTSPStream
 *
 * Get the the maximum time-to-live value of outgoing multicast packets.
 *
 * Returns: the maximum time-to-live value of outgoing multicast packets.
 *
 * Since: 1.16
 */
guint
gst_rtsp_stream_get_max_mcast_ttl (GstRTSPStream * stream)
{
  guint ttl;

  g_mutex_lock (&stream->priv->lock);
  ttl = stream->priv->max_mcast_ttl;
  g_mutex_unlock (&stream->priv->lock);

  return ttl;
}

/**
 * gst_rtsp_stream_verify_mcast_ttl:
 * @stream: a #GstRTSPStream
 * @ttl: a requested multicast ttl
 *
 * Check if the requested multicast ttl value is allowed.
 *
 * Returns: TRUE if the requested ttl value is allowed.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_stream_verify_mcast_ttl (GstRTSPStream * stream, guint ttl)
{
  gboolean res = FALSE;

  g_mutex_lock (&stream->priv->lock);
  if ((ttl > 0) && (ttl <= stream->priv->max_mcast_ttl))
    res = TRUE;
  g_mutex_unlock (&stream->priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_set_bind_mcast_address:
 * @stream: a #GstRTSPStream,
 * @bind_mcast_addr: the new value
 *
 * Decide whether the multicast socket should be bound to a multicast address or
 * INADDR_ANY.
 *
 * Since: 1.16
 */
void
gst_rtsp_stream_set_bind_mcast_address (GstRTSPStream * stream,
    gboolean bind_mcast_addr)
{
  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  g_mutex_lock (&stream->priv->lock);
  stream->priv->bind_mcast_address = bind_mcast_addr;
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_is_bind_mcast_address:
 * @stream: a #GstRTSPStream
 *
 * Check if multicast sockets are configured to be bound to multicast addresses.
 *
 * Returns: %TRUE if multicast sockets are configured to be bound to multicast addresses.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_stream_is_bind_mcast_address (GstRTSPStream * stream)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  g_mutex_lock (&stream->priv->lock);
  result = stream->priv->bind_mcast_address;
  g_mutex_unlock (&stream->priv->lock);

  return result;
}

void
gst_rtsp_stream_set_enable_rtcp (GstRTSPStream * stream, gboolean enable)
{
  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  g_mutex_lock (&stream->priv->lock);
  stream->priv->enable_rtcp = enable;
  g_mutex_unlock (&stream->priv->lock);
}

/* executed from streaming thread */
static void
caps_notify (GstPad * pad, GParamSpec * unused, GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstCaps *newcaps, *oldcaps;

  newcaps = gst_pad_get_current_caps (pad);

  GST_INFO ("stream %p received caps %p, %" GST_PTR_FORMAT, stream, newcaps,
      newcaps);

  g_mutex_lock (&priv->lock);
  oldcaps = priv->caps;
  priv->caps = newcaps;
  g_mutex_unlock (&priv->lock);

  if (oldcaps)
    gst_caps_unref (oldcaps);
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
  GstRTSPStreamPrivate *priv = stream->priv;
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

  g_mutex_lock (&priv->lock);
  GST_INFO ("finding %s:%d in %d transports", dest, port,
      g_list_length (priv->transports));

  for (walk = priv->transports; walk; walk = g_list_next (walk)) {
    GstRTSPStreamTransport *trans = walk->data;
    const GstRTSPTransport *tr;
    gint min, max;

    tr = gst_rtsp_stream_transport_get_transport (trans);

    if (priv->client_side) {
      /* In client side mode the 'destination' is the RTSP server, so send
       * to those ports */
      min = tr->server_port.min;
      max = tr->server_port.max;
    } else {
      min = tr->client_port.min;
      max = tr->client_port.max;
    }

    if ((g_ascii_strcasecmp (tr->destination, dest) == 0) &&
        (min == port || max == port)) {
      result = trans;
      break;
    }
  }
  if (result)
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

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
        g_object_set_qdata_full (source, ssrc_stream_map_key, trans,
            g_object_unref);
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

  if (trans) {
    GST_INFO ("%p: source %p in transport %p is active", stream, source, trans);
    gst_rtsp_stream_transport_keep_alive (trans);
  }
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
    gst_rtsp_stream_transport_set_timed_out (trans, TRUE);
    g_object_set_qdata (source, ssrc_stream_map_key, NULL);
  }
}

static void
on_timeout (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GstRTSPStreamTransport *trans;

  GST_INFO ("%p: source %p timeout", stream, source);

  if ((trans = g_object_get_qdata (source, ssrc_stream_map_key))) {
    gst_rtsp_stream_transport_set_timed_out (trans, TRUE);
    g_object_set_qdata (source, ssrc_stream_map_key, NULL);
  }
}

static void
on_new_sender_ssrc (GObject * session, GObject * source, GstRTSPStream * stream)
{
  GST_INFO ("%p: new sender source %p", stream, source);
#ifndef DUMP_STATS
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
on_sender_ssrc_active (GObject * session, GObject * source,
    GstRTSPStream * stream)
{
#ifndef DUMP_STATS
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
clear_tr_cache (GstRTSPStreamPrivate * priv)
{
  if (priv->tr_cache)
    g_ptr_array_unref (priv->tr_cache);
  priv->tr_cache = NULL;
}

/* With lock taken */
static gboolean
any_transport_ready (GstRTSPStream * stream, gboolean is_rtp)
{
  gboolean ret = TRUE;
  GstRTSPStreamPrivate *priv = stream->priv;
  GPtrArray *transports;
  gint index;

  transports = priv->tr_cache;

  if (!transports)
    goto done;

  for (index = 0; index < transports->len; index++) {
    GstRTSPStreamTransport *tr = g_ptr_array_index (transports, index);
    if (!gst_rtsp_stream_transport_check_back_pressure (tr, is_rtp)) {
      ret = TRUE;
      break;
    } else {
      ret = FALSE;
    }
  }

done:
  return ret;
}

/* Must be called *without* priv->lock */
static gboolean
push_data (GstRTSPStream * stream, GstRTSPStreamTransport * trans,
    GstBuffer * buffer, GstBufferList * buffer_list, gboolean is_rtp)
{
  gboolean send_ret = TRUE;

  if (is_rtp) {
    if (buffer)
      send_ret = gst_rtsp_stream_transport_send_rtp (trans, buffer);
    if (buffer_list)
      send_ret = gst_rtsp_stream_transport_send_rtp_list (trans, buffer_list);
  } else {
    if (buffer)
      send_ret = gst_rtsp_stream_transport_send_rtcp (trans, buffer);
    if (buffer_list)
      send_ret = gst_rtsp_stream_transport_send_rtcp_list (trans, buffer_list);
  }

  return send_ret;
}

/* With priv->lock */
static void
ensure_cached_transports (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GList *walk;

  if (priv->tr_cache_cookie != priv->transports_cookie) {
    clear_tr_cache (priv);
    priv->tr_cache =
        g_ptr_array_new_full (priv->n_tcp_transports, g_object_unref);

    for (walk = priv->transports; walk; walk = g_list_next (walk)) {
      GstRTSPStreamTransport *tr = (GstRTSPStreamTransport *) walk->data;
      const GstRTSPTransport *t = gst_rtsp_stream_transport_get_transport (tr);

      if (t->lower_transport != GST_RTSP_LOWER_TRANS_TCP)
        continue;

      g_ptr_array_add (priv->tr_cache, g_object_ref (tr));
    }
    priv->tr_cache_cookie = priv->transports_cookie;
  }
}

/* Must be called *without* priv->lock */
static void
check_transport_backlog (GstRTSPStream * stream, GstRTSPStreamTransport * trans)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  gboolean send_ret = TRUE;

  gst_rtsp_stream_transport_lock_backlog (trans);

  if (!gst_rtsp_stream_transport_backlog_is_empty (trans)) {
    GstBuffer *buffer;
    GstBufferList *buffer_list;
    gboolean is_rtp;
    gboolean popped;

    is_rtp = gst_rtsp_stream_transport_backlog_peek_is_rtp (trans);

    if (!gst_rtsp_stream_transport_check_back_pressure (trans, is_rtp)) {
      popped =
          gst_rtsp_stream_transport_backlog_pop (trans, &buffer, &buffer_list,
          &is_rtp);

      g_assert (popped == TRUE);

      send_ret = push_data (stream, trans, buffer, buffer_list, is_rtp);

      gst_clear_buffer (&buffer);
      gst_clear_buffer_list (&buffer_list);
    }
  }

  gst_rtsp_stream_transport_unlock_backlog (trans);

  if (!send_ret) {
    /* remove transport on send error */
    g_mutex_lock (&priv->lock);
    update_transport (stream, trans, FALSE);
    g_mutex_unlock (&priv->lock);
  }
}

/* Must be called with priv->lock */
static void
send_tcp_message (GstRTSPStream * stream, gint idx)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstAppSink *sink;
  GstSample *sample;
  GstBuffer *buffer;
  GstBufferList *buffer_list;
  gboolean is_rtp;
  GPtrArray *transports;

  if (!priv->have_buffer[idx])
    return;

  ensure_cached_transports (stream);

  is_rtp = (idx == 0);

  if (!any_transport_ready (stream, is_rtp))
    return;

  priv->have_buffer[idx] = FALSE;

  if (priv->appsink[idx] == NULL) {
    /* session expired */
    return;
  }

  sink = GST_APP_SINK (priv->appsink[idx]);
  sample = gst_app_sink_pull_sample (sink);
  if (!sample) {
    return;
  }

  buffer = gst_sample_get_buffer (sample);
  buffer_list = gst_sample_get_buffer_list (sample);

  /* We will get one message-sent notification per buffer or
   * complete buffer-list. We handle each buffer-list as a unit */

  transports = priv->tr_cache;
  if (transports)
    g_ptr_array_ref (transports);

  if (transports) {
    gint index;

    for (index = 0; index < transports->len; index++) {
      GstRTSPStreamTransport *tr = g_ptr_array_index (transports, index);
      GstBuffer *buf_ref = NULL;
      GstBufferList *buflist_ref = NULL;

      gst_rtsp_stream_transport_lock_backlog (tr);

      if (buffer)
        buf_ref = gst_buffer_ref (buffer);
      if (buffer_list)
        buflist_ref = gst_buffer_list_ref (buffer_list);

      if (!gst_rtsp_stream_transport_backlog_push (tr,
              buf_ref, buflist_ref, is_rtp)) {
        GST_ERROR_OBJECT (stream,
            "Dropping slow transport %" GST_PTR_FORMAT, tr);
        update_transport (stream, tr, FALSE);
      }

      gst_rtsp_stream_transport_unlock_backlog (tr);
    }
  }
  gst_sample_unref (sample);

  g_mutex_unlock (&priv->lock);

  if (transports) {
    gint index;

    for (index = 0; index < transports->len; index++) {
      GstRTSPStreamTransport *tr = g_ptr_array_index (transports, index);

      check_transport_backlog (stream, tr);
    }
    g_ptr_array_unref (transports);
  }

  g_mutex_lock (&priv->lock);
}

static gpointer
send_func (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;

  g_mutex_lock (&priv->send_lock);

  while (priv->continue_sending) {
    int i;
    int idx = -1;
    guint cookie;

    cookie = priv->send_cookie;
    g_mutex_unlock (&priv->send_lock);

    g_mutex_lock (&priv->lock);

    /* iterate from 1 and down, so we prioritize RTCP over RTP */
    for (i = 1; i >= 0; i--) {
      if (priv->have_buffer[i]) {
        /* send message */
        idx = i;
        break;
      }
    }

    if (idx != -1) {
      send_tcp_message (stream, idx);
    }

    g_mutex_unlock (&priv->lock);

    g_mutex_lock (&priv->send_lock);
    while (cookie == priv->send_cookie && priv->continue_sending) {
      g_cond_wait (&priv->send_cond, &priv->send_lock);
    }
  }

  g_mutex_unlock (&priv->send_lock);

  return NULL;
}

static GstFlowReturn
handle_new_sample (GstAppSink * sink, gpointer user_data)
{
  GstRTSPStream *stream = user_data;
  GstRTSPStreamPrivate *priv = stream->priv;
  int i;

  g_mutex_lock (&priv->lock);

  for (i = 0; i < 2; i++) {
    if (GST_ELEMENT_CAST (sink) == priv->appsink[i]) {
      priv->have_buffer[i] = TRUE;
      break;
    }
  }

  if (priv->send_thread == NULL) {
    priv->send_thread = g_thread_new (NULL, (GThreadFunc) send_func, user_data);
  }

  g_mutex_unlock (&priv->lock);

  g_mutex_lock (&priv->send_lock);
  priv->send_cookie++;
  g_cond_signal (&priv->send_cond);
  g_mutex_unlock (&priv->send_lock);

  return GST_FLOW_OK;
}

static GstAppSinkCallbacks sink_cb = {
  NULL,                         /* not interested in EOS */
  NULL,                         /* not interested in preroll samples */
  handle_new_sample,
};

static GstElement *
get_rtp_encoder (GstRTSPStream * stream, guint session)
{
  GstRTSPStreamPrivate *priv = stream->priv;

  if (priv->srtpenc == NULL) {
    gchar *name;

    name = g_strdup_printf ("srtpenc_%u", session);
    priv->srtpenc = gst_element_factory_make ("srtpenc", name);
    g_free (name);

    g_object_set (priv->srtpenc, "random-key", TRUE, NULL);
  }
  return gst_object_ref (priv->srtpenc);
}

static GstElement *
request_rtp_encoder (GstElement * rtpbin, guint session, GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstElement *oldenc, *enc;
  GstPad *pad;
  gchar *name;

  if (priv->idx != session)
    return NULL;

  GST_DEBUG_OBJECT (stream, "make RTP encoder for session %u", session);

  oldenc = priv->srtpenc;
  enc = get_rtp_encoder (stream, session);
  name = g_strdup_printf ("rtp_sink_%d", session);
  pad = gst_element_request_pad_simple (enc, name);
  g_free (name);
  gst_object_unref (pad);

  if (oldenc == NULL)
    g_signal_emit (stream, gst_rtsp_stream_signals[SIGNAL_NEW_RTP_ENCODER], 0,
        enc);

  return enc;
}

static GstElement *
request_rtcp_encoder (GstElement * rtpbin, guint session,
    GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstElement *oldenc, *enc;
  GstPad *pad;
  gchar *name;

  if (priv->idx != session)
    return NULL;

  GST_DEBUG_OBJECT (stream, "make RTCP encoder for session %u", session);

  oldenc = priv->srtpenc;
  enc = get_rtp_encoder (stream, session);
  name = g_strdup_printf ("rtcp_sink_%d", session);
  pad = gst_element_request_pad_simple (enc, name);
  g_free (name);
  gst_object_unref (pad);

  if (oldenc == NULL)
    g_signal_emit (stream, gst_rtsp_stream_signals[SIGNAL_NEW_RTCP_ENCODER], 0,
        enc);

  return enc;
}

static GstCaps *
request_key (GstElement * srtpdec, guint ssrc, GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstCaps *caps;

  GST_DEBUG ("request key %08x", ssrc);

  g_mutex_lock (&priv->lock);
  if ((caps = g_hash_table_lookup (priv->keys, GINT_TO_POINTER (ssrc))))
    gst_caps_ref (caps);
  g_mutex_unlock (&priv->lock);

  return caps;
}

static GstElement *
request_rtp_rtcp_decoder (GstElement * rtpbin, guint session,
    GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;

  if (priv->idx != session)
    return NULL;

  if (priv->srtpdec == NULL) {
    gchar *name;

    name = g_strdup_printf ("srtpdec_%u", session);
    priv->srtpdec = gst_element_factory_make ("srtpdec", name);
    g_free (name);

    g_signal_connect (priv->srtpdec, "request-key",
        (GCallback) request_key, stream);

    g_signal_emit (stream, gst_rtsp_stream_signals[SIGNAL_NEW_RTP_RTCP_DECODER],
        0, priv->srtpdec);

  }
  return gst_object_ref (priv->srtpdec);
}

/**
 * gst_rtsp_stream_request_aux_sender:
 * @stream: a #GstRTSPStream
 * @sessid: the session id
 *
 * Creating a rtxsend bin
 *
 * Returns: (transfer full) (nullable): a #GstElement.
 *
 * Since: 1.6
 */
GstElement *
gst_rtsp_stream_request_aux_sender (GstRTSPStream * stream, guint sessid)
{
  GstElement *bin;
  GstPad *pad;
  GstStructure *pt_map;
  gchar *name;
  guint pt, rtx_pt;
  gchar *pt_s;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  pt = gst_rtsp_stream_get_pt (stream);
  pt_s = g_strdup_printf ("%u", pt);
  rtx_pt = stream->priv->rtx_pt;

  GST_INFO ("creating rtxsend with pt %u to %u", pt, rtx_pt);

  bin = gst_bin_new (NULL);
  stream->priv->rtxsend = gst_element_factory_make ("rtprtxsend", NULL);
  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      pt_s, G_TYPE_UINT, rtx_pt, NULL);
  g_object_set (stream->priv->rtxsend, "payload-type-map", pt_map,
      "max-size-time", GST_TIME_AS_MSECONDS (stream->priv->rtx_time), NULL);
  g_free (pt_s);
  gst_structure_free (pt_map);
  gst_bin_add (GST_BIN (bin), gst_object_ref (stream->priv->rtxsend));

  pad = gst_element_get_static_pad (stream->priv->rtxsend, "src");
  name = g_strdup_printf ("src_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (stream->priv->rtxsend, "sink");
  name = g_strdup_printf ("sink_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  return bin;
}

static void
add_rtx_pt (gpointer key, GstCaps * caps, GstStructure * pt_map)
{
  guint pt = GPOINTER_TO_INT (key);
  const GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *apt;

  if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"), "RTX") &&
      (apt = gst_structure_get_string (s, "apt"))) {
    gst_structure_set (pt_map, apt, G_TYPE_UINT, pt, NULL);
  }
}

/* Call with priv->lock taken */
static void
update_rtx_receive_pt_map (GstRTSPStream * stream)
{
  GstStructure *pt_map;

  if (!stream->priv->rtxreceive)
    goto done;

  pt_map = gst_structure_new_empty ("application/x-rtp-pt-map");
  g_hash_table_foreach (stream->priv->ptmap, (GHFunc) add_rtx_pt, pt_map);
  g_object_set (stream->priv->rtxreceive, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);

done:
  return;
}

static void
retrieve_ulpfec_pt (gpointer key, GstCaps * caps, GstElement * ulpfec_decoder)
{
  guint pt = GPOINTER_TO_INT (key);
  const GstStructure *s = gst_caps_get_structure (caps, 0);

  if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"), "ULPFEC"))
    g_object_set (ulpfec_decoder, "pt", pt, NULL);
}

static void
update_ulpfec_decoder_pt (GstRTSPStream * stream)
{
  if (!stream->priv->ulpfec_decoder)
    goto done;

  g_hash_table_foreach (stream->priv->ptmap, (GHFunc) retrieve_ulpfec_pt,
      stream->priv->ulpfec_decoder);

done:
  return;
}

/**
 * gst_rtsp_stream_request_aux_receiver:
 * @stream: a #GstRTSPStream
 * @sessid: the session id
 *
 * Creating a rtxreceive bin
 *
 * Returns: (transfer full) (nullable): a #GstElement.
 *
 * Since: 1.16
 */
GstElement *
gst_rtsp_stream_request_aux_receiver (GstRTSPStream * stream, guint sessid)
{
  GstElement *bin;
  GstPad *pad;
  gchar *name;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  bin = gst_bin_new (NULL);
  stream->priv->rtxreceive = gst_element_factory_make ("rtprtxreceive", NULL);
  update_rtx_receive_pt_map (stream);
  update_ulpfec_decoder_pt (stream);
  gst_bin_add (GST_BIN (bin), gst_object_ref (stream->priv->rtxreceive));

  pad = gst_element_get_static_pad (stream->priv->rtxreceive, "src");
  name = g_strdup_printf ("src_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (stream->priv->rtxreceive, "sink");
  name = g_strdup_printf ("sink_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  return bin;
}

/**
 * gst_rtsp_stream_set_pt_map:
 * @stream: a #GstRTSPStream
 * @pt: the pt
 * @caps: a #GstCaps
 *
 * Configure a pt map between @pt and @caps.
 */
void
gst_rtsp_stream_set_pt_map (GstRTSPStream * stream, guint pt, GstCaps * caps)
{
  GstRTSPStreamPrivate *priv = stream->priv;

  if (!GST_IS_CAPS (caps))
    return;

  g_mutex_lock (&priv->lock);
  g_hash_table_insert (priv->ptmap, GINT_TO_POINTER (pt), gst_caps_ref (caps));
  update_rtx_receive_pt_map (stream);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_set_publish_clock_mode:
 * @stream: a #GstRTSPStream
 * @mode: the clock publish mode
 *
 * Sets if and how the stream clock should be published according to RFC7273.
 *
 * Since: 1.8
 */
void
gst_rtsp_stream_set_publish_clock_mode (GstRTSPStream * stream,
    GstRTSPPublishClockMode mode)
{
  GstRTSPStreamPrivate *priv;

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  priv->publish_clock_mode = mode;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_get_publish_clock_mode:
 * @stream: a #GstRTSPStream
 *
 * Gets if and how the stream clock should be published according to RFC7273.
 *
 * Returns: The GstRTSPPublishClockMode
 *
 * Since: 1.8
 */
GstRTSPPublishClockMode
gst_rtsp_stream_get_publish_clock_mode (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPPublishClockMode ret;

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  ret = priv->publish_clock_mode;
  g_mutex_unlock (&priv->lock);

  return ret;
}

static GstCaps *
request_pt_map (GstElement * rtpbin, guint session, guint pt,
    GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstCaps *caps = NULL;

  g_mutex_lock (&priv->lock);

  if (priv->idx == session) {
    caps = g_hash_table_lookup (priv->ptmap, GINT_TO_POINTER (pt));
    if (caps) {
      GST_DEBUG ("Stream %p, pt %u: caps %" GST_PTR_FORMAT, stream, pt, caps);
      gst_caps_ref (caps);
    } else {
      GST_DEBUG ("Stream %p, pt %u: no caps", stream, pt);
    }
  }

  g_mutex_unlock (&priv->lock);

  return caps;
}

static void
pad_added (GstElement * rtpbin, GstPad * pad, GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  gchar *name;
  GstPadLinkReturn ret;
  guint sessid;

  GST_DEBUG ("Stream %p added pad %s:%s for pad %s:%s", stream,
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (priv->sinkpad));

  name = gst_pad_get_name (pad);
  if (sscanf (name, "recv_rtp_src_%u", &sessid) != 1) {
    g_free (name);
    return;
  }
  g_free (name);

  if (priv->idx != sessid)
    return;

  if (gst_pad_is_linked (priv->sinkpad)) {
    GST_WARNING ("Stream %p: Pad %s:%s is linked already", stream,
        GST_DEBUG_PAD_NAME (priv->sinkpad));
    return;
  }

  /* link the RTP pad to the session manager, it should not really fail unless
   * this is not really an RTP pad */
  ret = gst_pad_link (pad, priv->sinkpad);
  if (ret != GST_PAD_LINK_OK)
    goto link_failed;
  priv->recv_rtp_src = gst_object_ref (pad);

  return;

/* ERRORS */
link_failed:
  {
    GST_ERROR ("Stream %p: Failed to link pads %s:%s and %s:%s", stream,
        GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (priv->sinkpad));
  }
}

static void
on_npt_stop (GstElement * rtpbin, guint session, guint ssrc,
    GstRTSPStream * stream)
{
  /* TODO: What to do here other than this? */
  GST_DEBUG ("Stream %p: Got EOS", stream);
  gst_pad_send_event (stream->priv->sinkpad, gst_event_new_eos ());
}

typedef struct _ProbeData ProbeData;

struct _ProbeData
{
  GstRTSPStream *stream;
  /* existing sink, already linked to tee */
  GstElement *sink1;
  /* new sink, about to be linked */
  GstElement *sink2;
  /* new queue element, that will be linked to tee and sink1 */
  GstElement **queue1;
  /* new queue element, that will be linked to tee and sink2 */
  GstElement **queue2;
  GstPad *sink_pad;
  GstPad *tee_pad;
  guint index;
};

static void
free_cb_data (gpointer user_data)
{
  ProbeData *data = user_data;

  gst_object_unref (data->stream);
  gst_object_unref (data->sink1);
  gst_object_unref (data->sink2);
  gst_object_unref (data->sink_pad);
  gst_object_unref (data->tee_pad);
  g_free (data);
}


static void
create_and_plug_queue_to_unlinked_stream (GstRTSPStream * stream,
    GstElement * tee, GstElement * sink, GstElement ** queue)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstPad *tee_pad;
  GstPad *queue_pad;
  GstPad *sink_pad;

  /* create queue for the new stream */
  *queue = gst_element_factory_make ("queue", NULL);
  g_object_set (*queue, "max-size-buffers", 1, "max-size-bytes", 0,
      "max-size-time", G_GINT64_CONSTANT (0), NULL);
  gst_bin_add (priv->joined_bin, *queue);

  /* link tee to queue */
  tee_pad = gst_element_request_pad_simple (tee, "src_%u");
  queue_pad = gst_element_get_static_pad (*queue, "sink");
  gst_pad_link (tee_pad, queue_pad);
  gst_object_unref (queue_pad);
  gst_object_unref (tee_pad);

  /* link queue to sink */
  queue_pad = gst_element_get_static_pad (*queue, "src");
  sink_pad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (queue_pad, sink_pad);
  gst_object_unref (queue_pad);
  gst_object_unref (sink_pad);

  gst_element_sync_state_with_parent (sink);
  gst_element_sync_state_with_parent (*queue);
}

static GstPadProbeReturn
create_and_plug_queue_to_linked_stream_probe_cb (GstPad * inpad,
    GstPadProbeInfo * info, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  ProbeData *data = user_data;
  GstRTSPStream *stream;
  GstElement **queue1;
  GstElement **queue2;
  GstPad *sink_pad;
  GstPad *tee_pad;
  GstPad *queue_pad;
  guint index;

  stream = data->stream;
  priv = stream->priv;
  queue1 = data->queue1;
  queue2 = data->queue2;
  sink_pad = data->sink_pad;
  tee_pad = data->tee_pad;
  index = data->index;

  /* unlink tee and the existing sink:
   *   .-----.    .---------.
   *   | tee |    |  sink1  |
   * sink   src->sink       |
   *   '-----'    '---------'
   */
  g_assert (gst_pad_unlink (tee_pad, sink_pad));

  /* add queue to the already existing stream */
  *queue1 = gst_element_factory_make ("queue", NULL);
  g_object_set (*queue1, "max-size-buffers", 1, "max-size-bytes", 0,
      "max-size-time", G_GINT64_CONSTANT (0), NULL);
  gst_bin_add (priv->joined_bin, *queue1);

  /* link tee, queue and sink:
   *   .-----.    .---------.    .---------.
   *   | tee |    |  queue1 |    | sink1   |
   * sink   src->sink      src->sink       |
   *   '-----'    '---------'    '---------'
   */
  queue_pad = gst_element_get_static_pad (*queue1, "sink");
  gst_pad_link (tee_pad, queue_pad);
  gst_object_unref (queue_pad);
  queue_pad = gst_element_get_static_pad (*queue1, "src");
  gst_pad_link (queue_pad, sink_pad);
  gst_object_unref (queue_pad);

  gst_element_sync_state_with_parent (*queue1);

  /* create queue and link it to tee and the new sink */
  create_and_plug_queue_to_unlinked_stream (stream,
      priv->tee[index], data->sink2, queue2);

  /* the final stream:
   *
   *    .-----.    .---------.    .---------.
   *    | tee |    |  queue1 |    | sink1   |
   *  sink   src->sink      src->sink       |
   *    |     |    '---------'    '---------'
   *    |     |    .---------.    .---------.
   *    |     |    |  queue2 |    | sink2   |
   *    |    src->sink      src->sink       |
   *    '-----'    '---------'    '---------'
   */

  return GST_PAD_PROBE_REMOVE;
}

static void
create_and_plug_queue_to_linked_stream (GstRTSPStream * stream,
    GstElement * sink1, GstElement * sink2, guint index, GstElement ** queue1,
    GstElement ** queue2)
{
  ProbeData *data;

  data = g_new0 (ProbeData, 1);
  data->stream = gst_object_ref (stream);
  data->sink1 = gst_object_ref (sink1);
  data->sink2 = gst_object_ref (sink2);
  data->queue1 = queue1;
  data->queue2 = queue2;
  data->index = index;

  data->sink_pad = gst_element_get_static_pad (sink1, "sink");
  g_assert (data->sink_pad);
  data->tee_pad = gst_pad_get_peer (data->sink_pad);
  g_assert (data->tee_pad);

  gst_pad_add_probe (data->tee_pad, GST_PAD_PROBE_TYPE_IDLE,
      create_and_plug_queue_to_linked_stream_probe_cb, data, free_cb_data);
}

static void
plug_udp_sink (GstRTSPStream * stream, GstElement * sink_to_plug,
    GstElement ** queue_to_plug, guint index, gboolean is_mcast)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstElement *existing_sink;

  if (is_mcast)
    existing_sink = priv->udpsink[index];
  else
    existing_sink = priv->mcast_udpsink[index];

  GST_DEBUG_OBJECT (stream, "plug %s sink", is_mcast ? "mcast" : "udp");

  /* add sink to the bin */
  gst_bin_add (priv->joined_bin, sink_to_plug);

  if (priv->appsink[index] && existing_sink) {

    /* queues are already added for the existing stream, add one for
       the newly added udp stream */
    create_and_plug_queue_to_unlinked_stream (stream, priv->tee[index],
        sink_to_plug, queue_to_plug);

  } else if (priv->appsink[index] || existing_sink) {
    GstElement **queue;
    GstElement *element;

    /* add queue to the already existing stream plus the newly created udp
       stream */
    if (priv->appsink[index]) {
      element = priv->appsink[index];
      queue = &priv->appqueue[index];
    } else {
      element = existing_sink;
      if (is_mcast)
        queue = &priv->udpqueue[index];
      else
        queue = &priv->mcast_udpqueue[index];
    }

    create_and_plug_queue_to_linked_stream (stream, element, sink_to_plug,
        index, queue, queue_to_plug);

  } else {
    GstPad *tee_pad;
    GstPad *sink_pad;

    GST_DEBUG_OBJECT (stream, "creating first stream");

    /* no need to add queues */
    tee_pad = gst_element_request_pad_simple (priv->tee[index], "src_%u");
    sink_pad = gst_element_get_static_pad (sink_to_plug, "sink");
    gst_pad_link (tee_pad, sink_pad);
    gst_object_unref (tee_pad);
    gst_object_unref (sink_pad);
  }

  gst_element_sync_state_with_parent (sink_to_plug);
}

static void
plug_tcp_sink (GstRTSPStream * stream, guint index)
{
  GstRTSPStreamPrivate *priv = stream->priv;

  GST_DEBUG_OBJECT (stream, "plug tcp sink");

  /* add sink to the bin */
  gst_bin_add (priv->joined_bin, priv->appsink[index]);

  if (priv->mcast_udpsink[index] && priv->udpsink[index]) {

    /* queues are already added for the existing stream, add one for
       the newly added tcp stream */
    create_and_plug_queue_to_unlinked_stream (stream,
        priv->tee[index], priv->appsink[index], &priv->appqueue[index]);

  } else if (priv->mcast_udpsink[index] || priv->udpsink[index]) {
    GstElement **queue;
    GstElement *element;

    /* add queue to the already existing stream plus the newly created tcp
       stream */
    if (priv->mcast_udpsink[index]) {
      element = priv->mcast_udpsink[index];
      queue = &priv->mcast_udpqueue[index];
    } else {
      element = priv->udpsink[index];
      queue = &priv->udpqueue[index];
    }

    create_and_plug_queue_to_linked_stream (stream, element,
        priv->appsink[index], index, queue, &priv->appqueue[index]);

  } else {
    GstPad *tee_pad;
    GstPad *sink_pad;

    /* no need to add queues */
    tee_pad = gst_element_request_pad_simple (priv->tee[index], "src_%u");
    sink_pad = gst_element_get_static_pad (priv->appsink[index], "sink");
    gst_pad_link (tee_pad, sink_pad);
    gst_object_unref (tee_pad);
    gst_object_unref (sink_pad);
  }

  gst_element_sync_state_with_parent (priv->appsink[index]);
}

static void
plug_sink (GstRTSPStream * stream, const GstRTSPTransport * transport,
    guint index)
{
  GstRTSPStreamPrivate *priv;
  gboolean is_tcp, is_udp, is_mcast;
  priv = stream->priv;

  is_tcp = transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP;
  is_udp = transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP;
  is_mcast = transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST;

  if (is_udp)
    plug_udp_sink (stream, priv->udpsink[index],
        &priv->udpqueue[index], index, FALSE);

  else if (is_mcast)
    plug_udp_sink (stream, priv->mcast_udpsink[index],
        &priv->mcast_udpqueue[index], index, TRUE);

  else if (is_tcp)
    plug_tcp_sink (stream, index);
}

/* must be called with lock */
static gboolean
create_sender_part (GstRTSPStream * stream, const GstRTSPTransport * transport)
{
  GstRTSPStreamPrivate *priv;
  GstPad *pad;
  GstBin *bin;
  gboolean is_tcp, is_udp, is_mcast;
  gint mcast_ttl = 0;
  gint i;

  GST_DEBUG_OBJECT (stream, "create sender part");
  priv = stream->priv;
  bin = priv->joined_bin;

  is_tcp = transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP;
  is_udp = transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP;
  is_mcast = transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST;

  if (is_mcast)
    mcast_ttl = transport->ttl;

  GST_DEBUG_OBJECT (stream, "tcp: %d, udp: %d, mcast: %d (ttl: %d)", is_tcp,
      is_udp, is_mcast, mcast_ttl);

  if (is_udp && !priv->server_addr_v4 && !priv->server_addr_v6) {
    GST_WARNING_OBJECT (stream, "no sockets assigned for UDP");
    return FALSE;
  }

  if (is_mcast && !priv->mcast_addr_v4 && !priv->mcast_addr_v6) {
    GST_WARNING_OBJECT (stream, "no sockets assigned for UDP multicast");
    return FALSE;
  }

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (priv->payloader),
          "onvif-no-rate-control"))
    g_object_set (priv->payloader, "onvif-no-rate-control",
        !priv->do_rate_control, NULL);

  for (i = 0; i < (priv->enable_rtcp ? 2 : 1); i++) {
    gboolean link_tee = FALSE;
    /* For the sender we create this bit of pipeline for both
     * RTP and RTCP (when enabled).
     * Initially there will be only one active transport for
     * the stream, so the pipeline will look like this:
     *
     * .--------.      .-----.    .---------.
     * | rtpbin |      | tee |    |  sink   |
     * |       send->sink   src->sink       |
     * '--------'      '-----'    '---------'
     *
     * For each new transport, the already existing branch will
     * be reconfigured by adding a queue element:
     *
     * .--------.      .-----.    .---------.    .---------.
     * | rtpbin |      | tee |    |  queue  |    | udpsink |
     * |       send->sink   src->sink      src->sink       |
     * '--------'      |     |    '---------'    '---------'
     *                 |     |    .---------.    .---------.
     *                 |     |    |  queue  |    | udpsink |
     *                 |    src->sink      src->sink       |
     *                 |     |    '---------'    '---------'
     *                 |     |    .---------.    .---------.
     *                 |     |    |  queue  |    | appsink |
     *                 |    src->sink      src->sink       |
     *                 '-----'    '---------'    '---------'
     */

    /* Only link the RTP send src if we're going to send RTP, link
     * the RTCP send src always */
    if (!priv->srcpad && i == 0)
      continue;

    if (!priv->tee[i]) {
      /* make tee for RTP/RTCP */
      priv->tee[i] = gst_element_factory_make ("tee", NULL);
      gst_bin_add (bin, priv->tee[i]);
      link_tee = TRUE;
    }

    if (is_udp && !priv->udpsink[i]) {
      /* we create only one pair of udpsinks for IPv4 and IPv6 */
      create_and_configure_udpsink (stream, &priv->udpsink[i],
          priv->socket_v4[i], priv->socket_v6[i], FALSE, (i == 0), mcast_ttl);
      plug_sink (stream, transport, i);
    } else if (is_mcast && !priv->mcast_udpsink[i]) {
      /* we create only one pair of mcast-udpsinks for IPv4 and IPv6 */
      create_and_configure_udpsink (stream, &priv->mcast_udpsink[i],
          priv->mcast_socket_v4[i], priv->mcast_socket_v6[i], TRUE, (i == 0),
          mcast_ttl);
      plug_sink (stream, transport, i);
    } else if (is_tcp && !priv->appsink[i]) {
      /* make appsink */
      priv->appsink[i] = gst_element_factory_make ("appsink", NULL);
      g_object_set (priv->appsink[i], "emit-signals", FALSE, "buffer-list",
          TRUE, "max-buffers", 1, NULL);

      if (i == 0)
        g_object_set (priv->appsink[i], "sync", priv->do_rate_control, NULL);

      /* we need to set sync and preroll to FALSE for the sink to avoid
       * deadlock. This is only needed for sink sending RTCP data. */
      if (i == 1)
        g_object_set (priv->appsink[i], "async", FALSE, "sync", FALSE, NULL);

      gst_app_sink_set_callbacks (GST_APP_SINK_CAST (priv->appsink[i]),
          &sink_cb, stream, NULL);
      plug_sink (stream, transport, i);
    }

    if (link_tee) {
      /* and link to rtpbin send pad */
      gst_element_sync_state_with_parent (priv->tee[i]);
      pad = gst_element_get_static_pad (priv->tee[i], "sink");
      gst_pad_link (priv->send_src[i], pad);
      gst_object_unref (pad);
    }
  }

  return TRUE;
}

/* must be called with lock */
static void
plug_src (GstRTSPStream * stream, GstBin * bin, GstElement * src,
    GstElement * funnel)
{
  GstRTSPStreamPrivate *priv;
  GstPad *pad, *selpad;
  gulong id = 0;

  priv = stream->priv;

  /* add src */
  gst_bin_add (bin, src);

  pad = gst_element_get_static_pad (src, "src");
  if (priv->srcpad) {
    /* block pad so src can't push data while it's not yet linked */
    id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK |
        GST_PAD_PROBE_TYPE_BUFFER, NULL, NULL, NULL);
    /* we set and keep these to playing so that they don't cause NO_PREROLL return
     * values. This is only relevant for PLAY pipelines */
    gst_element_set_state (src, GST_STATE_PLAYING);
    gst_element_set_locked_state (src, TRUE);
  }

  /* and link to the funnel */
  selpad = gst_element_request_pad_simple (funnel, "sink_%u");
  gst_pad_link (pad, selpad);
  if (id != 0)
    gst_pad_remove_probe (pad, id);
  gst_object_unref (pad);
  gst_object_unref (selpad);
}

/* must be called with lock */
static gboolean
create_receiver_part (GstRTSPStream * stream, const GstRTSPTransport *
    transport)
{
  gboolean ret = FALSE;
  GstRTSPStreamPrivate *priv;
  GstPad *pad;
  GstBin *bin;
  gboolean tcp;
  gboolean udp;
  gboolean mcast;
  gboolean secure;
  gint i;
  GstCaps *rtp_caps;
  GstCaps *rtcp_caps;

  GST_DEBUG_OBJECT (stream, "create receiver part");
  priv = stream->priv;
  bin = priv->joined_bin;

  tcp = transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP;
  udp = transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP;
  mcast = transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST;
  secure = (priv->profiles & GST_RTSP_PROFILE_SAVP)
      || (priv->profiles & GST_RTSP_PROFILE_SAVPF);

  if (secure) {
    rtp_caps = gst_caps_new_empty_simple ("application/x-srtp");
    rtcp_caps = gst_caps_new_empty_simple ("application/x-srtcp");
  } else {
    rtp_caps = gst_caps_new_empty_simple ("application/x-rtp");
    rtcp_caps = gst_caps_new_empty_simple ("application/x-rtcp");
  }

  GST_DEBUG_OBJECT (stream,
      "RTP caps: %" GST_PTR_FORMAT " RTCP caps: %" GST_PTR_FORMAT, rtp_caps,
      rtcp_caps);

  for (i = 0; i < (priv->enable_rtcp ? 2 : 1); i++) {
    /* For the receiver we create this bit of pipeline for both
     * RTP and RTCP (when enabled). We receive RTP/RTCP on appsrc and udpsrc
     * and it is all funneled into the rtpbin receive pad.
     *
     *
     * .--------.     .--------.    .--------.
     * | udpsrc |     | funnel |    | rtpbin |
     * | RTP    src->sink      src->sink     |
     * '--------'     |        |    |        |
     * .--------.     |        |    |        |
     * | appsrc |     |        |    |        |
     * | RTP    src->sink      |    |        |
     * '--------'     '--------'    |        |
     *                              |        |
     * .--------.     .--------.    |        |
     * | udpsrc |     | funnel |    |        |
     * | RTCP   src->sink      src->sink     |
     * '--------'     |        |    '--------'
     * .--------.     |        |
     * | appsrc |     |        |
     * | RTCP   src->sink      |
     * '--------'     '--------'
     */

    if (!priv->sinkpad && i == 0) {
      /* Only connect recv RTP sink if we expect to receive RTP. Connect recv
       * RTCP sink always */
      continue;
    }

    /* make funnel for the RTP/RTCP receivers */
    if (!priv->funnel[i]) {
      priv->funnel[i] = gst_element_factory_make ("funnel", NULL);
      gst_bin_add (bin, priv->funnel[i]);

      pad = gst_element_get_static_pad (priv->funnel[i], "src");
      gst_pad_link (pad, priv->recv_sink[i]);
      gst_object_unref (pad);
    }

    if (udp && !priv->udpsrc_v4[i] && priv->server_addr_v4) {
      GST_DEBUG_OBJECT (stream, "udp IPv4, create and configure udpsources");
      if (!create_and_configure_udpsource (&priv->udpsrc_v4[i],
              priv->socket_v4[i]))
        goto done;

      if (i == 0) {
        g_object_set (priv->udpsrc_v4[i], "caps", rtp_caps, NULL);
      } else {
        g_object_set (priv->udpsrc_v4[i], "caps", rtcp_caps, NULL);

        /* block early rtcp packets, pipeline not ready */
        g_assert (priv->block_early_rtcp_pad == NULL);
        priv->block_early_rtcp_pad = gst_element_get_static_pad
            (priv->udpsrc_v4[i], "src");
        priv->block_early_rtcp_probe = gst_pad_add_probe
            (priv->block_early_rtcp_pad,
            GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER, NULL, NULL,
            NULL);
      }

      plug_src (stream, bin, priv->udpsrc_v4[i], priv->funnel[i]);
    }

    if (udp && !priv->udpsrc_v6[i] && priv->server_addr_v6) {
      GST_DEBUG_OBJECT (stream, "udp IPv6, create and configure udpsources");
      if (!create_and_configure_udpsource (&priv->udpsrc_v6[i],
              priv->socket_v6[i]))
        goto done;

      if (i == 0) {
        g_object_set (priv->udpsrc_v6[i], "caps", rtp_caps, NULL);
      } else {
        g_object_set (priv->udpsrc_v6[i], "caps", rtcp_caps, NULL);

        /* block early rtcp packets, pipeline not ready */
        g_assert (priv->block_early_rtcp_pad_ipv6 == NULL);
        priv->block_early_rtcp_pad_ipv6 = gst_element_get_static_pad
            (priv->udpsrc_v6[i], "src");
        priv->block_early_rtcp_probe_ipv6 = gst_pad_add_probe
            (priv->block_early_rtcp_pad_ipv6,
            GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER, NULL, NULL,
            NULL);
      }

      plug_src (stream, bin, priv->udpsrc_v6[i], priv->funnel[i]);
    }

    if (mcast && !priv->mcast_udpsrc_v4[i] && priv->mcast_addr_v4) {
      GST_DEBUG_OBJECT (stream, "mcast IPv4, create and configure udpsources");
      if (!create_and_configure_udpsource (&priv->mcast_udpsrc_v4[i],
              priv->mcast_socket_v4[i]))
        goto done;

      if (i == 0) {
        g_object_set (priv->mcast_udpsrc_v4[i], "caps", rtp_caps, NULL);
      } else {
        g_object_set (priv->mcast_udpsrc_v4[i], "caps", rtcp_caps, NULL);
      }

      plug_src (stream, bin, priv->mcast_udpsrc_v4[i], priv->funnel[i]);
    }

    if (mcast && !priv->mcast_udpsrc_v6[i] && priv->mcast_addr_v6) {
      GST_DEBUG_OBJECT (stream, "mcast IPv6, create and configure udpsources");
      if (!create_and_configure_udpsource (&priv->mcast_udpsrc_v6[i],
              priv->mcast_socket_v6[i]))
        goto done;

      if (i == 0) {
        g_object_set (priv->mcast_udpsrc_v6[i], "caps", rtp_caps, NULL);
      } else {
        g_object_set (priv->mcast_udpsrc_v6[i], "caps", rtcp_caps, NULL);
      }

      plug_src (stream, bin, priv->mcast_udpsrc_v6[i], priv->funnel[i]);
    }

    if (tcp && !priv->appsrc[i]) {
      /* make and add appsrc */
      priv->appsrc[i] = gst_element_factory_make ("appsrc", NULL);
      priv->appsrc_base_time[i] = -1;
      g_object_set (priv->appsrc[i], "format", GST_FORMAT_TIME, "is-live",
          TRUE, NULL);
      plug_src (stream, bin, priv->appsrc[i], priv->funnel[i]);
    }

    gst_element_sync_state_with_parent (priv->funnel[i]);
  }

  ret = TRUE;

done:
  gst_caps_unref (rtp_caps);
  gst_caps_unref (rtcp_caps);
  return ret;
}

gboolean
gst_rtsp_stream_is_tcp_receiver (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gboolean ret = FALSE;

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  ret = (priv->sinkpad != NULL && priv->appsrc[0] != NULL);
  g_mutex_unlock (&priv->lock);

  return ret;
}

static gboolean
check_mcast_client_addr (GstRTSPStream * stream, const GstRTSPTransport * tr)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GList *walk;

  if (priv->mcast_clients == NULL)
    goto no_addr;

  if (tr == NULL)
    goto no_transport;

  if (tr->destination == NULL)
    goto no_destination;

  for (walk = priv->mcast_clients; walk; walk = g_list_next (walk)) {
    UdpClientAddrInfo *cli = walk->data;

    if ((g_strcmp0 (cli->address, tr->destination) == 0) &&
        (cli->rtp_port == tr->port.min))
      return TRUE;
  }

  return FALSE;

no_addr:
  {
    GST_WARNING_OBJECT (stream, "Adding mcast transport, but no mcast address "
        "has been reserved");
    return FALSE;
  }
no_transport:
  {
    GST_WARNING_OBJECT (stream, "Adding mcast transport, but no transport "
        "has been provided");
    return FALSE;
  }
no_destination:
  {
    GST_WARNING_OBJECT (stream, "Adding mcast transport, but it doesn't match "
        "the reserved address");
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_join_bin:
 * @stream: a #GstRTSPStream
 * @bin: (transfer none): a #GstBin to join
 * @rtpbin: (transfer none): a rtpbin element in @bin
 * @state: the target state of the new elements
 *
 * Join the #GstBin @bin that contains the element @rtpbin.
 *
 * @stream will link to @rtpbin, which must be inside @bin. The elements
 * added to @bin will be set to the state given in @state.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_stream_join_bin (GstRTSPStream * stream, GstBin * bin,
    GstElement * rtpbin, GstState state)
{
  GstRTSPStreamPrivate *priv;
  guint idx;
  gchar *name;
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (rtpbin), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if (priv->joined_bin != NULL)
    goto was_joined;

  /* create a session with the same index as the stream */
  idx = priv->idx;

  GST_INFO ("stream %p joining bin as session %u", stream, idx);

  if (priv->profiles & GST_RTSP_PROFILE_SAVP
      || priv->profiles & GST_RTSP_PROFILE_SAVPF) {
    /* For SRTP */
    g_signal_connect (rtpbin, "request-rtp-encoder",
        (GCallback) request_rtp_encoder, stream);
    g_signal_connect (rtpbin, "request-rtcp-encoder",
        (GCallback) request_rtcp_encoder, stream);
    g_signal_connect (rtpbin, "request-rtp-decoder",
        (GCallback) request_rtp_rtcp_decoder, stream);
    g_signal_connect (rtpbin, "request-rtcp-decoder",
        (GCallback) request_rtp_rtcp_decoder, stream);
  }

  if (priv->sinkpad) {
    g_signal_connect (rtpbin, "request-pt-map",
        (GCallback) request_pt_map, stream);
  }

  /* get pads from the RTP session element for sending and receiving
   * RTP/RTCP*/
  if (priv->srcpad) {
    /* get a pad for sending RTP */
    name = g_strdup_printf ("send_rtp_sink_%u", idx);
    priv->send_rtp_sink = gst_element_request_pad_simple (rtpbin, name);
    g_free (name);

    /* link the RTP pad to the session manager, it should not really fail unless
     * this is not really an RTP pad */
    ret = gst_pad_link (priv->srcpad, priv->send_rtp_sink);
    if (ret != GST_PAD_LINK_OK)
      goto link_failed;

    name = g_strdup_printf ("send_rtp_src_%u", idx);
    priv->send_src[0] = gst_element_get_static_pad (rtpbin, name);
    g_free (name);
  } else {
    /* RECORD case: need to connect our sinkpad from here */
    g_signal_connect (rtpbin, "pad-added", (GCallback) pad_added, stream);
    /* EOS */
    g_signal_connect (rtpbin, "on-npt-stop", (GCallback) on_npt_stop, stream);

    name = g_strdup_printf ("recv_rtp_sink_%u", idx);
    priv->recv_sink[0] = gst_element_request_pad_simple (rtpbin, name);
    g_free (name);
  }

  if (priv->enable_rtcp) {
    name = g_strdup_printf ("send_rtcp_src_%u", idx);
    priv->send_src[1] = gst_element_request_pad_simple (rtpbin, name);
    g_free (name);

    name = g_strdup_printf ("recv_rtcp_sink_%u", idx);
    priv->recv_sink[1] = gst_element_request_pad_simple (rtpbin, name);
    g_free (name);
  }

  /* get the session */
  g_signal_emit_by_name (rtpbin, "get-internal-session", idx, &priv->session);

  g_signal_connect (priv->session, "on-new-ssrc", (GCallback) on_new_ssrc,
      stream);
  g_signal_connect (priv->session, "on-ssrc-sdes", (GCallback) on_ssrc_sdes,
      stream);
  g_signal_connect (priv->session, "on-ssrc-active",
      (GCallback) on_ssrc_active, stream);
  g_signal_connect (priv->session, "on-bye-ssrc", (GCallback) on_bye_ssrc,
      stream);
  g_signal_connect (priv->session, "on-bye-timeout",
      (GCallback) on_bye_timeout, stream);
  g_signal_connect (priv->session, "on-timeout", (GCallback) on_timeout,
      stream);

  /* signal for sender ssrc */
  g_signal_connect (priv->session, "on-new-sender-ssrc",
      (GCallback) on_new_sender_ssrc, stream);
  g_signal_connect (priv->session, "on-sender-ssrc-active",
      (GCallback) on_sender_ssrc_active, stream);

  g_object_set (priv->session, "disable-sr-timestamp", !priv->do_rate_control,
      NULL);

  if (priv->srcpad) {
    /* be notified of caps changes */
    priv->caps_sig = g_signal_connect (priv->send_src[0], "notify::caps",
        (GCallback) caps_notify, stream);
    priv->caps = gst_pad_get_current_caps (priv->send_src[0]);
  }

  priv->joined_bin = bin;
  GST_DEBUG_OBJECT (stream, "successfully joined bin");
  g_mutex_unlock (&priv->lock);

  return TRUE;

  /* ERRORS */
was_joined:
  {
    g_mutex_unlock (&priv->lock);
    return TRUE;
  }
link_failed:
  {
    GST_WARNING ("failed to link stream %u", idx);
    gst_object_unref (priv->send_rtp_sink);
    priv->send_rtp_sink = NULL;
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
}

static void
clear_element (GstBin * bin, GstElement ** elementptr)
{
  if (*elementptr) {
    gst_element_set_locked_state (*elementptr, FALSE);
    gst_element_set_state (*elementptr, GST_STATE_NULL);
    if (GST_ELEMENT_PARENT (*elementptr))
      gst_bin_remove (bin, *elementptr);
    else
      gst_object_unref (*elementptr);
    *elementptr = NULL;
  }
}

/**
 * gst_rtsp_stream_leave_bin:
 * @stream: a #GstRTSPStream
 * @bin: (transfer none): a #GstBin
 * @rtpbin: (transfer none): a rtpbin #GstElement
 *
 * Remove the elements of @stream from @bin.
 *
 * Return: %TRUE on success.
 */
gboolean
gst_rtsp_stream_leave_bin (GstRTSPStream * stream, GstBin * bin,
    GstElement * rtpbin)
{
  GstRTSPStreamPrivate *priv;
  gint i;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (rtpbin), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->send_lock);
  priv->continue_sending = FALSE;
  priv->send_cookie++;
  g_cond_signal (&priv->send_cond);
  g_mutex_unlock (&priv->send_lock);

  if (priv->send_thread) {
    g_thread_join (priv->send_thread);
  }

  g_mutex_lock (&priv->lock);
  if (priv->joined_bin == NULL)
    goto was_not_joined;
  if (priv->joined_bin != bin)
    goto wrong_bin;

  priv->joined_bin = NULL;

  /* all transports must be removed by now */
  if (priv->transports != NULL)
    goto transports_not_removed;

  if (priv->send_pool) {
    GThreadPool *slask;

    slask = priv->send_pool;
    priv->send_pool = NULL;
    g_mutex_unlock (&priv->lock);
    g_thread_pool_free (slask, TRUE, TRUE);
    g_mutex_lock (&priv->lock);
  }

  clear_tr_cache (priv);

  GST_INFO ("stream %p leaving bin", stream);

  if (priv->srcpad) {
    gst_pad_unlink (priv->srcpad, priv->send_rtp_sink);

    g_signal_handler_disconnect (priv->send_src[0], priv->caps_sig);
    gst_element_release_request_pad (rtpbin, priv->send_rtp_sink);
    gst_object_unref (priv->send_rtp_sink);
    priv->send_rtp_sink = NULL;
  } else if (priv->recv_rtp_src) {
    gst_pad_unlink (priv->recv_rtp_src, priv->sinkpad);
    gst_object_unref (priv->recv_rtp_src);
    priv->recv_rtp_src = NULL;
  }

  for (i = 0; i < (priv->enable_rtcp ? 2 : 1); i++) {
    clear_element (bin, &priv->udpsrc_v4[i]);
    clear_element (bin, &priv->udpsrc_v6[i]);
    clear_element (bin, &priv->udpqueue[i]);
    clear_element (bin, &priv->udpsink[i]);

    clear_element (bin, &priv->mcast_udpsrc_v4[i]);
    clear_element (bin, &priv->mcast_udpsrc_v6[i]);
    clear_element (bin, &priv->mcast_udpqueue[i]);
    clear_element (bin, &priv->mcast_udpsink[i]);

    clear_element (bin, &priv->appsrc[i]);
    clear_element (bin, &priv->appqueue[i]);
    clear_element (bin, &priv->appsink[i]);

    clear_element (bin, &priv->tee[i]);
    clear_element (bin, &priv->funnel[i]);

    if (priv->sinkpad || i == 1) {
      gst_element_release_request_pad (rtpbin, priv->recv_sink[i]);
      gst_object_unref (priv->recv_sink[i]);
      priv->recv_sink[i] = NULL;
    }
  }

  if (priv->srcpad) {
    gst_object_unref (priv->send_src[0]);
    priv->send_src[0] = NULL;
  }

  if (priv->enable_rtcp) {
    gst_element_release_request_pad (rtpbin, priv->send_src[1]);
    gst_object_unref (priv->send_src[1]);
    priv->send_src[1] = NULL;
  }

  g_object_unref (priv->session);
  priv->session = NULL;
  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = NULL;

  if (priv->srtpenc)
    gst_object_unref (priv->srtpenc);
  if (priv->srtpdec)
    gst_object_unref (priv->srtpdec);

  if (priv->mcast_addr_v4)
    gst_rtsp_address_free (priv->mcast_addr_v4);
  priv->mcast_addr_v4 = NULL;
  if (priv->mcast_addr_v6)
    gst_rtsp_address_free (priv->mcast_addr_v6);
  priv->mcast_addr_v6 = NULL;
  if (priv->server_addr_v4)
    gst_rtsp_address_free (priv->server_addr_v4);
  priv->server_addr_v4 = NULL;
  if (priv->server_addr_v6)
    gst_rtsp_address_free (priv->server_addr_v6);
  priv->server_addr_v6 = NULL;

  for (i = 0; i < 2; i++) {
    g_clear_object (&priv->socket_v4[i]);
    g_clear_object (&priv->socket_v6[i]);
    g_clear_object (&priv->mcast_socket_v4[i]);
    g_clear_object (&priv->mcast_socket_v6[i]);
  }

  g_mutex_unlock (&priv->lock);

  return TRUE;

was_not_joined:
  {
    g_mutex_unlock (&priv->lock);
    return TRUE;
  }
transports_not_removed:
  {
    GST_ERROR_OBJECT (stream, "can't leave bin (transports not removed)");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
wrong_bin:
  {
    GST_ERROR_OBJECT (stream, "leaving the wrong bin");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_get_joined_bin:
 * @stream: a #GstRTSPStream
 *
 * Get the previous joined bin with gst_rtsp_stream_join_bin() or NULL.
 *
 * Return: (transfer full) (nullable): the joined bin or NULL.
 */
GstBin *
gst_rtsp_stream_get_joined_bin (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstBin *bin = NULL;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  bin = priv->joined_bin ? gst_object_ref (priv->joined_bin) : NULL;
  g_mutex_unlock (&priv->lock);

  return bin;
}

/**
 * gst_rtsp_stream_get_rtpinfo:
 * @stream: a #GstRTSPStream
 * @rtptime: (allow-none) (out caller-allocates): result RTP timestamp
 * @seq: (allow-none) (out caller-allocates): result RTP seqnum
 * @clock_rate: (allow-none) (out caller-allocates): the clock rate
 * @running_time: (out caller-allocates): result running-time
 *
 * Retrieve the current rtptime, seq and running-time. This is used to
 * construct a RTPInfo reply header.
 *
 * Returns: %TRUE when rtptime, seq and running-time could be determined.
 */
gboolean
gst_rtsp_stream_get_rtpinfo (GstRTSPStream * stream,
    guint * rtptime, guint * seq, guint * clock_rate,
    GstClockTime * running_time)
{
  GstRTSPStreamPrivate *priv;
  GstStructure *stats;
  GObjectClass *payobjclass;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  payobjclass = G_OBJECT_GET_CLASS (priv->payloader);

  g_mutex_lock (&priv->lock);

  /* First try to extract the information from the last buffer on the sinks.
   * This will have a more accurate sequence number and timestamp, as between
   * the payloader and the sink there can be some queues
   */
  if (priv->udpsink[0] || priv->mcast_udpsink[0] || priv->appsink[0]) {
    GstSample *last_sample;

    if (priv->udpsink[0])
      g_object_get (priv->udpsink[0], "last-sample", &last_sample, NULL);
    else if (priv->mcast_udpsink[0])
      g_object_get (priv->mcast_udpsink[0], "last-sample", &last_sample, NULL);
    else
      g_object_get (priv->appsink[0], "last-sample", &last_sample, NULL);

    if (last_sample && !priv->blocking) {
      GstCaps *caps;
      GstBuffer *buffer;
      GstSegment *segment;
      GstStructure *s;
      GstRTPBuffer rtp_buffer = GST_RTP_BUFFER_INIT;

      caps = gst_sample_get_caps (last_sample);
      buffer = gst_sample_get_buffer (last_sample);
      segment = gst_sample_get_segment (last_sample);
      s = gst_caps_get_structure (caps, 0);

      if (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp_buffer)) {
        guint ssrc_buf = gst_rtp_buffer_get_ssrc (&rtp_buffer);
        guint ssrc_stream = 0;
        if (gst_structure_has_field_typed (s, "ssrc", G_TYPE_UINT) &&
            gst_structure_get_uint (s, "ssrc", &ssrc_stream) &&
            ssrc_buf != ssrc_stream) {
          /* Skip buffers from auxiliary streams. */
          GST_DEBUG_OBJECT (stream,
              "not a buffer from the payloader, SSRC: %08x", ssrc_buf);

          gst_rtp_buffer_unmap (&rtp_buffer);
          gst_sample_unref (last_sample);
          goto stats;
        }

        if (seq) {
          *seq = gst_rtp_buffer_get_seq (&rtp_buffer);
        }

        if (rtptime) {
          *rtptime = gst_rtp_buffer_get_timestamp (&rtp_buffer);
        }

        gst_rtp_buffer_unmap (&rtp_buffer);

        if (running_time) {
          *running_time =
              gst_segment_to_running_time (segment, GST_FORMAT_TIME,
              GST_BUFFER_TIMESTAMP (buffer));
        }

        if (clock_rate) {
          gst_structure_get_int (s, "clock-rate", (gint *) clock_rate);

          if (*clock_rate == 0 && running_time)
            *running_time = GST_CLOCK_TIME_NONE;
        }
        gst_sample_unref (last_sample);

        goto done;
      } else {
        gst_sample_unref (last_sample);
      }
    } else if (priv->blocking) {
      if (last_sample != NULL)
        gst_sample_unref (last_sample);
      if (seq) {
        if (!priv->blocked_buffer)
          goto stats;
        *seq = priv->blocked_seqnum;
      }

      if (rtptime) {
        if (!priv->blocked_buffer)
          goto stats;
        *rtptime = priv->blocked_rtptime;
      }

      if (running_time) {
        if (!GST_CLOCK_TIME_IS_VALID (priv->blocked_running_time))
          goto stats;
        *running_time = priv->blocked_running_time;
      }

      if (clock_rate) {
        *clock_rate = priv->blocked_clock_rate;

        if (*clock_rate == 0 && running_time)
          *running_time = GST_CLOCK_TIME_NONE;
      }

      goto done;
    }
  }

stats:
  if (g_object_class_find_property (payobjclass, "stats")) {
    g_object_get (priv->payloader, "stats", &stats, NULL);
    if (stats == NULL)
      goto no_stats;

    if (seq)
      gst_structure_get_uint (stats, "seqnum-offset", seq);

    if (rtptime)
      gst_structure_get_uint (stats, "timestamp", rtptime);

    if (running_time)
      gst_structure_get_clock_time (stats, "running-time", running_time);

    if (clock_rate) {
      gst_structure_get_uint (stats, "clock-rate", clock_rate);
      if (*clock_rate == 0 && running_time)
        *running_time = GST_CLOCK_TIME_NONE;
    }
    gst_structure_free (stats);
  } else {
    if (!g_object_class_find_property (payobjclass, "seqnum") ||
        !g_object_class_find_property (payobjclass, "timestamp"))
      goto no_stats;

    if (seq)
      g_object_get (priv->payloader, "seqnum", seq, NULL);

    if (rtptime)
      g_object_get (priv->payloader, "timestamp", rtptime, NULL);

    if (running_time)
      *running_time = GST_CLOCK_TIME_NONE;
  }

done:
  g_mutex_unlock (&priv->lock);

  return TRUE;

  /* ERRORS */
no_stats:
  {
    GST_WARNING ("Could not get payloader stats");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_get_rates:
 * @stream: a #GstRTSPStream
 * @rate: (optional) (out caller-allocates): the configured rate
 * @applied_rate: (optional) (out caller-allocates): the configured applied_rate
 *
 * Retrieve the current rate and/or applied_rate.
 *
 * Returns: %TRUE if rate and/or applied_rate could be determined.
 * Since: 1.18
 */
gboolean
gst_rtsp_stream_get_rates (GstRTSPStream * stream, gdouble * rate,
    gdouble * applied_rate)
{
  GstRTSPStreamPrivate *priv;
  GstEvent *event;
  const GstSegment *segment;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  if (!rate && !applied_rate) {
    GST_WARNING_OBJECT (stream, "rate and applied_rate are both NULL");
    return FALSE;
  }

  priv = stream->priv;

  g_mutex_lock (&priv->lock);

  if (!priv->send_rtp_sink)
    goto no_rtp_sink_pad;

  event = gst_pad_get_sticky_event (priv->send_rtp_sink, GST_EVENT_SEGMENT, 0);
  if (!event)
    goto no_sticky_event;

  gst_event_parse_segment (event, &segment);
  if (rate)
    *rate = segment->rate;
  if (applied_rate)
    *applied_rate = segment->applied_rate;

  gst_event_unref (event);
  g_mutex_unlock (&priv->lock);

  return TRUE;

/* ERRORS */
no_rtp_sink_pad:
  {
    GST_WARNING_OBJECT (stream, "no send_rtp_sink pad yet");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
no_sticky_event:
  {
    GST_WARNING_OBJECT (stream, "no segment event on send_rtp_sink pad");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }

}

/**
 * gst_rtsp_stream_get_caps:
 * @stream: a #GstRTSPStream
 *
 * Retrieve the current caps of @stream.
 *
 * Returns: (transfer full) (nullable): the #GstCaps of @stream.
 * use gst_caps_unref() after usage.
 */
GstCaps *
gst_rtsp_stream_get_caps (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstCaps *result;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->caps))
    gst_caps_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
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
  GstRTSPStreamPrivate *priv;
  GstFlowReturn ret;
  GstElement *element;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), GST_FLOW_ERROR);
  priv = stream->priv;
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);
  g_return_val_if_fail (priv->joined_bin != NULL, FALSE);

  g_mutex_lock (&priv->lock);
  if (priv->appsrc[0])
    element = gst_object_ref (priv->appsrc[0]);
  else
    element = NULL;
  g_mutex_unlock (&priv->lock);

  if (element) {
    if (priv->appsrc_base_time[0] == -1) {
      /* Take current running_time. This timestamp will be put on
       * the first buffer of each stream because we are a live source and so we
       * timestamp with the running_time. When we are dealing with TCP, we also
       * only timestamp the first buffer (using the DISCONT flag) because a server
       * typically bursts data, for which we don't want to compensate by speeding
       * up the media. The other timestamps will be interpollated from this one
       * using the RTP timestamps. */
      GST_OBJECT_LOCK (element);
      if (GST_ELEMENT_CLOCK (element)) {
        GstClockTime now;
        GstClockTime base_time;

        now = gst_clock_get_time (GST_ELEMENT_CLOCK (element));
        base_time = GST_ELEMENT_CAST (element)->base_time;

        priv->appsrc_base_time[0] = now - base_time;
        GST_BUFFER_TIMESTAMP (buffer) = priv->appsrc_base_time[0];
        GST_DEBUG ("stream %p: first buffer at time %" GST_TIME_FORMAT
            ", base %" GST_TIME_FORMAT, stream, GST_TIME_ARGS (now),
            GST_TIME_ARGS (base_time));
      }
      GST_OBJECT_UNLOCK (element);
    }

    ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (element), buffer);
    gst_object_unref (element);
  } else {
    ret = GST_FLOW_OK;
  }
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
  GstRTSPStreamPrivate *priv;
  GstFlowReturn ret;
  GstElement *element;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), GST_FLOW_ERROR);
  priv = stream->priv;
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  if (priv->joined_bin == NULL) {
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_LINKED;
  }
  g_mutex_lock (&priv->lock);
  if (priv->appsrc[1])
    element = gst_object_ref (priv->appsrc[1]);
  else
    element = NULL;
  g_mutex_unlock (&priv->lock);

  if (element) {
    if (priv->appsrc_base_time[1] == -1) {
      /* Take current running_time. This timestamp will be put on
       * the first buffer of each stream because we are a live source and so we
       * timestamp with the running_time. When we are dealing with TCP, we also
       * only timestamp the first buffer (using the DISCONT flag) because a server
       * typically bursts data, for which we don't want to compensate by speeding
       * up the media. The other timestamps will be interpollated from this one
       * using the RTP timestamps. */
      GST_OBJECT_LOCK (element);
      if (GST_ELEMENT_CLOCK (element)) {
        GstClockTime now;
        GstClockTime base_time;

        now = gst_clock_get_time (GST_ELEMENT_CLOCK (element));
        base_time = GST_ELEMENT_CAST (element)->base_time;

        priv->appsrc_base_time[1] = now - base_time;
        GST_BUFFER_TIMESTAMP (buffer) = priv->appsrc_base_time[1];
        GST_DEBUG ("stream %p: first buffer at time %" GST_TIME_FORMAT
            ", base %" GST_TIME_FORMAT, stream, GST_TIME_ARGS (now),
            GST_TIME_ARGS (base_time));
      }
      GST_OBJECT_UNLOCK (element);
    }

    ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (element), buffer);
    gst_object_unref (element);
  } else {
    ret = GST_FLOW_OK;
    gst_buffer_unref (buffer);
  }
  return ret;
}

/* must be called with lock */
static inline void
add_client (GstElement * rtp_sink, GstElement * rtcp_sink, const gchar * host,
    gint rtp_port, gint rtcp_port)
{
  if (rtp_sink != NULL)
    g_signal_emit_by_name (rtp_sink, "add", host, rtp_port, NULL);
  if (rtcp_sink != NULL)
    g_signal_emit_by_name (rtcp_sink, "add", host, rtcp_port, NULL);
}

/* must be called with lock */
static void
remove_client (GstElement * rtp_sink, GstElement * rtcp_sink,
    const gchar * host, gint rtp_port, gint rtcp_port)
{
  if (rtp_sink != NULL)
    g_signal_emit_by_name (rtp_sink, "remove", host, rtp_port, NULL);
  if (rtcp_sink != NULL)
    g_signal_emit_by_name (rtcp_sink, "remove", host, rtcp_port, NULL);
}

/* must be called with lock */
static gboolean
update_transport (GstRTSPStream * stream, GstRTSPStreamTransport * trans,
    gboolean add)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  const GstRTSPTransport *tr;
  gchar *dest;
  gint min, max;
  GList *tr_element;

  tr = gst_rtsp_stream_transport_get_transport (trans);
  dest = tr->destination;

  tr_element = g_list_find (priv->transports, trans);

  if (add && tr_element)
    return TRUE;
  else if (!add && !tr_element)
    return FALSE;

  switch (tr->lower_transport) {
    case GST_RTSP_LOWER_TRANS_UDP_MCAST:
    {
      min = tr->port.min;
      max = tr->port.max;

      if (add) {
        GST_INFO ("adding %s:%d-%d", dest, min, max);
        if (!check_mcast_client_addr (stream, tr))
          goto mcast_error;
        add_client (priv->mcast_udpsink[0], priv->mcast_udpsink[1], dest, min,
            max);

        if (tr->ttl > 0) {
          GST_INFO ("setting ttl-mc %d", tr->ttl);
          if (priv->mcast_udpsink[0])
            g_object_set (G_OBJECT (priv->mcast_udpsink[0]), "ttl-mc", tr->ttl,
                NULL);
          if (priv->mcast_udpsink[1])
            g_object_set (G_OBJECT (priv->mcast_udpsink[1]), "ttl-mc", tr->ttl,
                NULL);
        }
        priv->transports = g_list_prepend (priv->transports, trans);
      } else {
        GST_INFO ("removing %s:%d-%d", dest, min, max);
        if (!remove_mcast_client_addr (stream, dest, min, max))
          GST_WARNING_OBJECT (stream,
              "Failed to remove multicast address: %s:%d-%d", dest, min, max);
        priv->transports = g_list_delete_link (priv->transports, tr_element);
        remove_client (priv->mcast_udpsink[0], priv->mcast_udpsink[1], dest,
            min, max);
      }
      break;
    }
    case GST_RTSP_LOWER_TRANS_UDP:
    {
      if (priv->client_side) {
        /* In client side mode the 'destination' is the RTSP server, so send
         * to those ports */
        min = tr->server_port.min;
        max = tr->server_port.max;
      } else {
        min = tr->client_port.min;
        max = tr->client_port.max;
      }

      if (add) {
        GST_INFO ("adding %s:%d-%d", dest, min, max);
        add_client (priv->udpsink[0], priv->udpsink[1], dest, min, max);
        priv->transports = g_list_prepend (priv->transports, trans);
      } else {
        GST_INFO ("removing %s:%d-%d", dest, min, max);
        priv->transports = g_list_delete_link (priv->transports, tr_element);
        remove_client (priv->udpsink[0], priv->udpsink[1], dest, min, max);
      }
      priv->transports_cookie++;
      break;
    }
    case GST_RTSP_LOWER_TRANS_TCP:
      if (add) {
        GST_INFO ("adding TCP %s", tr->destination);
        priv->transports = g_list_prepend (priv->transports, trans);
        priv->n_tcp_transports++;
      } else {
        GST_INFO ("removing TCP %s", tr->destination);
        priv->transports = g_list_delete_link (priv->transports, tr_element);

        gst_rtsp_stream_transport_lock_backlog (trans);
        gst_rtsp_stream_transport_clear_backlog (trans);
        gst_rtsp_stream_transport_unlock_backlog (trans);

        priv->n_tcp_transports--;
      }
      priv->transports_cookie++;
      break;
    default:
      goto unknown_transport;
  }
  return TRUE;

  /* ERRORS */
unknown_transport:
  {
    GST_INFO ("Unknown transport %d", tr->lower_transport);
    return FALSE;
  }
mcast_error:
  {
    return FALSE;
  }
}

static void
on_message_sent (GstRTSPStreamTransport * trans, gpointer user_data)
{
  GstRTSPStream *stream = GST_RTSP_STREAM (user_data);
  GstRTSPStreamPrivate *priv = stream->priv;

  GST_DEBUG_OBJECT (stream, "message send complete");

  check_transport_backlog (stream, trans);

  g_mutex_lock (&priv->send_lock);
  priv->send_cookie++;
  g_cond_signal (&priv->send_cond);
  g_mutex_unlock (&priv->send_lock);
}

/**
 * gst_rtsp_stream_add_transport:
 * @stream: a #GstRTSPStream
 * @trans: (transfer none): a #GstRTSPStreamTransport
 *
 * Add the transport in @trans to @stream. The media of @stream will
 * then also be send to the values configured in @trans. Adding the
 * same transport twice will not add it a second time.
 *
 * @stream must be joined to a bin.
 *
 * @trans must contain a valid #GstRTSPTransport.
 *
 * Returns: %TRUE if @trans was added
 */
gboolean
gst_rtsp_stream_add_transport (GstRTSPStream * stream,
    GstRTSPStreamTransport * trans)
{
  GstRTSPStreamPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  priv = stream->priv;
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);
  g_return_val_if_fail (priv->joined_bin != NULL, FALSE);

  g_mutex_lock (&priv->lock);
  res = update_transport (stream, trans, TRUE);
  if (res)
    gst_rtsp_stream_transport_set_message_sent_full (trans, on_message_sent,
        stream, NULL);
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_remove_transport:
 * @stream: a #GstRTSPStream
 * @trans: (transfer none): a #GstRTSPStreamTransport
 *
 * Remove the transport in @trans from @stream. The media of @stream will
 * not be sent to the values configured in @trans.
 *
 * @stream must be joined to a bin.
 *
 * @trans must contain a valid #GstRTSPTransport.
 *
 * Returns: %TRUE if @trans was removed
 */
gboolean
gst_rtsp_stream_remove_transport (GstRTSPStream * stream,
    GstRTSPStreamTransport * trans)
{
  GstRTSPStreamPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  priv = stream->priv;
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);
  g_return_val_if_fail (priv->joined_bin != NULL, FALSE);

  g_mutex_lock (&priv->lock);
  res = update_transport (stream, trans, FALSE);
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_update_crypto:
 * @stream: a #GstRTSPStream
 * @ssrc: the SSRC
 * @crypto: (transfer none) (allow-none): a #GstCaps with crypto info
 *
 * Update the new crypto information for @ssrc in @stream. If information
 * for @ssrc did not exist, it will be added. If information
 * for @ssrc existed, it will be replaced. If @crypto is %NULL, it will
 * be removed from @stream.
 *
 * Returns: %TRUE if @crypto could be updated
 */
gboolean
gst_rtsp_stream_update_crypto (GstRTSPStream * stream,
    guint ssrc, GstCaps * crypto)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (crypto == NULL || GST_IS_CAPS (crypto), FALSE);

  priv = stream->priv;

  GST_DEBUG_OBJECT (stream, "update key for %08x", ssrc);

  g_mutex_lock (&priv->lock);
  if (crypto)
    g_hash_table_insert (priv->keys, GINT_TO_POINTER (ssrc),
        gst_caps_ref (crypto));
  else
    g_hash_table_remove (priv->keys, GINT_TO_POINTER (ssrc));
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_stream_get_rtp_socket:
 * @stream: a #GstRTSPStream
 * @family: the socket family
 *
 * Get the RTP socket from @stream for a @family.
 *
 * @stream must be joined to a bin.
 *
 * Returns: (transfer full) (nullable): the RTP socket or %NULL if no
 * socket could be allocated for @family. Unref after usage
 */
GSocket *
gst_rtsp_stream_get_rtp_socket (GstRTSPStream * stream, GSocketFamily family)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GSocket *socket;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (family == G_SOCKET_FAMILY_IPV4 ||
      family == G_SOCKET_FAMILY_IPV6, NULL);

  g_mutex_lock (&priv->lock);
  if (family == G_SOCKET_FAMILY_IPV6)
    socket = priv->socket_v6[0];
  else
    socket = priv->socket_v4[0];

  if (socket != NULL)
    socket = g_object_ref (socket);
  g_mutex_unlock (&priv->lock);

  return socket;
}

/**
 * gst_rtsp_stream_get_rtcp_socket:
 * @stream: a #GstRTSPStream
 * @family: the socket family
 *
 * Get the RTCP socket from @stream for a @family.
 *
 * @stream must be joined to a bin.
 *
 * Returns: (transfer full) (nullable): the RTCP socket or %NULL if no
 * socket could be allocated for @family. Unref after usage
 */
GSocket *
gst_rtsp_stream_get_rtcp_socket (GstRTSPStream * stream, GSocketFamily family)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GSocket *socket;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (family == G_SOCKET_FAMILY_IPV4 ||
      family == G_SOCKET_FAMILY_IPV6, NULL);

  g_mutex_lock (&priv->lock);
  if (family == G_SOCKET_FAMILY_IPV6)
    socket = priv->socket_v6[1];
  else
    socket = priv->socket_v4[1];

  if (socket != NULL)
    socket = g_object_ref (socket);
  g_mutex_unlock (&priv->lock);

  return socket;
}

/**
 * gst_rtsp_stream_get_rtp_multicast_socket:
 * @stream: a #GstRTSPStream
 * @family: the socket family
 *
 * Get the multicast RTP socket from @stream for a @family.
 *
 * Returns: (transfer full) (nullable): the multicast RTP socket or %NULL if no
 *
 * socket could be allocated for @family. Unref after usage
 */
GSocket *
gst_rtsp_stream_get_rtp_multicast_socket (GstRTSPStream * stream,
    GSocketFamily family)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GSocket *socket;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (family == G_SOCKET_FAMILY_IPV4 ||
      family == G_SOCKET_FAMILY_IPV6, NULL);

  g_mutex_lock (&priv->lock);
  if (family == G_SOCKET_FAMILY_IPV6)
    socket = priv->mcast_socket_v6[0];
  else
    socket = priv->mcast_socket_v4[0];

  if (socket != NULL)
    socket = g_object_ref (socket);
  g_mutex_unlock (&priv->lock);

  return socket;
}

/**
 * gst_rtsp_stream_get_rtcp_multicast_socket:
 * @stream: a #GstRTSPStream
 * @family: the socket family
 *
 * Get the multicast RTCP socket from @stream for a @family.
 *
 * Returns: (transfer full) (nullable): the multicast RTCP socket or %NULL if no
 * socket could be allocated for @family. Unref after usage
 *
 * Since: 1.14
 */
GSocket *
gst_rtsp_stream_get_rtcp_multicast_socket (GstRTSPStream * stream,
    GSocketFamily family)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GSocket *socket;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (family == G_SOCKET_FAMILY_IPV4 ||
      family == G_SOCKET_FAMILY_IPV6, NULL);

  g_mutex_lock (&priv->lock);
  if (family == G_SOCKET_FAMILY_IPV6)
    socket = priv->mcast_socket_v6[1];
  else
    socket = priv->mcast_socket_v4[1];

  if (socket != NULL)
    socket = g_object_ref (socket);
  g_mutex_unlock (&priv->lock);

  return socket;
}

/**
 * gst_rtsp_stream_add_multicast_client_address:
 * @stream: a #GstRTSPStream
 * @destination: (transfer none): a multicast address to add
 * @rtp_port: RTP port
 * @rtcp_port: RTCP port
 * @family: socket family
 *
 * Add multicast client address to stream. At this point, the sockets that
 * will stream RTP and RTCP data to @destination are supposed to be
 * allocated.
 *
 * Returns: %TRUE if @destination can be addedd and handled by @stream.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_stream_add_multicast_client_address (GstRTSPStream * stream,
    const gchar * destination, guint rtp_port, guint rtcp_port,
    GSocketFamily family)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);
  g_return_val_if_fail (destination != NULL, FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  if ((family == G_SOCKET_FAMILY_IPV4) && (priv->mcast_socket_v4[0] == NULL))
    goto socket_error;
  else if ((family == G_SOCKET_FAMILY_IPV6) &&
      (priv->mcast_socket_v6[0] == NULL))
    goto socket_error;

  if (!add_mcast_client_addr (stream, destination, rtp_port, rtcp_port))
    goto add_addr_error;
  g_mutex_unlock (&priv->lock);

  return TRUE;

socket_error:
  {
    GST_WARNING_OBJECT (stream,
        "Failed to add multicast address: no udp socket");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
add_addr_error:
  {
    GST_WARNING_OBJECT (stream,
        "Failed to add multicast address: invalid address");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_get_multicast_client_addresses
 * @stream: a #GstRTSPStream
 *
 * Get all multicast client addresses that RTP data will be sent to
 *
 * Returns: A comma separated list of host:port pairs with destinations
 *
 * Since: 1.16
 */
gchar *
gst_rtsp_stream_get_multicast_client_addresses (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GString *str;
  GList *clients;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;
  str = g_string_new ("");

  g_mutex_lock (&priv->lock);
  clients = priv->mcast_clients;
  while (clients != NULL) {
    UdpClientAddrInfo *client;

    client = (UdpClientAddrInfo *) clients->data;
    clients = g_list_next (clients);
    g_string_append_printf (str, "%s:%d%s", client->address, client->rtp_port,
        (clients != NULL ? "," : ""));
  }
  g_mutex_unlock (&priv->lock);

  return g_string_free (str, FALSE);
}

/**
 * gst_rtsp_stream_set_seqnum:
 * @stream: a #GstRTSPStream
 * @seqnum: a new sequence number
 *
 * Configure the sequence number in the payloader of @stream to @seqnum.
 */
void
gst_rtsp_stream_set_seqnum_offset (GstRTSPStream * stream, guint16 seqnum)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  priv = stream->priv;

  g_object_set (G_OBJECT (priv->payloader), "seqnum-offset", seqnum, NULL);
}

/**
 * gst_rtsp_stream_get_seqnum:
 * @stream: a #GstRTSPStream
 *
 * Get the configured sequence number in the payloader of @stream.
 *
 * Returns: the sequence number of the payloader.
 */
guint16
gst_rtsp_stream_get_current_seqnum (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  guint seqnum;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), 0);

  priv = stream->priv;

  g_object_get (G_OBJECT (priv->payloader), "seqnum", &seqnum, NULL);

  return seqnum;
}

/**
 * gst_rtsp_stream_transport_filter:
 * @stream: a #GstRTSPStream
 * @func: (scope call) (allow-none): a callback
 * @user_data: (closure): user data passed to @func
 *
 * Call @func for each transport managed by @stream. The result value of @func
 * determines what happens to the transport. @func will be called with @stream
 * locked so no further actions on @stream can be performed from @func.
 *
 * If @func returns #GST_RTSP_FILTER_REMOVE, the transport will be removed from
 * @stream.
 *
 * If @func returns #GST_RTSP_FILTER_KEEP, the transport will remain in @stream.
 *
 * If @func returns #GST_RTSP_FILTER_REF, the transport will remain in @stream but
 * will also be added with an additional ref to the result #GList of this
 * function..
 *
 * When @func is %NULL, #GST_RTSP_FILTER_REF will be assumed for each transport.
 *
 * Returns: (element-type GstRTSPStreamTransport) (transfer full): a #GList with all
 * transports for which @func returned #GST_RTSP_FILTER_REF. After usage, each
 * element in the #GList should be unreffed before the list is freed.
 */
GList *
gst_rtsp_stream_transport_filter (GstRTSPStream * stream,
    GstRTSPStreamTransportFilterFunc func, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  GList *result, *walk, *next;
  GHashTable *visited = NULL;
  guint cookie;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  priv = stream->priv;

  result = NULL;
  if (func)
    visited = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);

  g_mutex_lock (&priv->lock);
restart:
  cookie = priv->transports_cookie;
  for (walk = priv->transports; walk; walk = next) {
    GstRTSPStreamTransport *trans = walk->data;
    GstRTSPFilterResult res;
    gboolean changed;

    next = g_list_next (walk);

    if (func) {
      /* only visit each transport once */
      if (g_hash_table_contains (visited, trans))
        continue;

      g_hash_table_add (visited, g_object_ref (trans));
      g_mutex_unlock (&priv->lock);

      res = func (stream, trans, user_data);

      g_mutex_lock (&priv->lock);
    } else
      res = GST_RTSP_FILTER_REF;

    changed = (cookie != priv->transports_cookie);

    switch (res) {
      case GST_RTSP_FILTER_REMOVE:
        update_transport (stream, trans, FALSE);
        break;
      case GST_RTSP_FILTER_REF:
        result = g_list_prepend (result, g_object_ref (trans));
        break;
      case GST_RTSP_FILTER_KEEP:
      default:
        break;
    }
    if (changed)
      goto restart;
  }
  g_mutex_unlock (&priv->lock);

  if (func)
    g_hash_table_unref (visited);

  return result;
}

static GstPadProbeReturn
rtp_pad_blocking (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPStream *stream;
  GstBuffer *buffer = NULL;
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstEvent *event;

  stream = user_data;
  priv = stream->priv;

  g_mutex_lock (&priv->lock);

  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

    buffer = gst_pad_probe_info_get_buffer (info);
    if (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp)) {
      priv->blocked_buffer = TRUE;
      priv->blocked_seqnum = gst_rtp_buffer_get_seq (&rtp);
      priv->blocked_rtptime = gst_rtp_buffer_get_timestamp (&rtp);
      gst_rtp_buffer_unmap (&rtp);
    }
    priv->position = GST_BUFFER_TIMESTAMP (buffer);
    if (priv->drop_delta_units) {
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        g_assert (!priv->blocking);
        GST_DEBUG_OBJECT (pad, "dropping delta-unit buffer");
        ret = GST_PAD_PROBE_DROP;
        goto done;
      }
    }
  } else if ((info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST)) {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

    GstBufferList *list = gst_pad_probe_info_get_buffer_list (info);
    buffer = gst_buffer_list_get (list, 0);
    if (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp)) {
      priv->blocked_buffer = TRUE;
      priv->blocked_seqnum = gst_rtp_buffer_get_seq (&rtp);
      priv->blocked_rtptime = gst_rtp_buffer_get_timestamp (&rtp);
      gst_rtp_buffer_unmap (&rtp);
    }
    priv->position = GST_BUFFER_TIMESTAMP (buffer);
    if (priv->drop_delta_units) {
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        g_assert (!priv->blocking);
        GST_DEBUG_OBJECT (pad, "dropping delta-unit buffer");
        ret = GST_PAD_PROBE_DROP;
        goto done;
      }
    }
  } else if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)) {
    if (GST_EVENT_TYPE (info->data) == GST_EVENT_GAP) {
      gst_event_parse_gap (info->data, &priv->position, NULL);
    } else {
      ret = GST_PAD_PROBE_PASS;
      GST_WARNING ("Passing event.");
      goto done;
    }
  } else {
    g_assert_not_reached ();
  }

  event = gst_pad_get_sticky_event (pad, GST_EVENT_SEGMENT, 0);
  if (event) {
    const GstSegment *segment;

    gst_event_parse_segment (event, &segment);
    priv->blocked_running_time =
        gst_segment_to_stream_time (segment, GST_FORMAT_TIME, priv->position);
    gst_event_unref (event);
  }

  event = gst_pad_get_sticky_event (pad, GST_EVENT_CAPS, 0);
  if (event) {
    GstCaps *caps;
    GstStructure *s;

    gst_event_parse_caps (event, &caps);
    s = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (s, "clock-rate", &priv->blocked_clock_rate);
    gst_event_unref (event);
  }

  /* make sure to block on the correct frame type */
  if (priv->drop_delta_units) {
    g_assert (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));
  }

  priv->blocking = TRUE;

  GST_DEBUG_OBJECT (pad, "Now blocking");

  GST_DEBUG_OBJECT (stream, "position: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (priv->position));

  gst_element_post_message (priv->payloader,
      gst_message_new_element (GST_OBJECT_CAST (priv->payloader),
          gst_structure_new ("GstRTSPStreamBlocking", "is_complete",
              G_TYPE_BOOLEAN, priv->is_complete, NULL)));
done:
  g_mutex_unlock (&priv->lock);
  return ret;
}

/* this probe will drop a single buffer. It is used when an old buffer is
 * blocking the pipeline, such as between a DESCRIBE and a PLAY request. */
static GstPadProbeReturn
drop_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPStream *stream;
  /* drop an old buffer stuck in a blocked pipeline */
  GstPadProbeReturn ret = GST_PAD_PROBE_DROP;

  stream = user_data;
  priv = stream->priv;

  g_mutex_lock (&priv->lock);

  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER ||
          info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST)) {
    /* if a buffer has been dropped then remove this probe */
    if (priv->remove_drop_probe) {
      priv->remove_drop_probe = FALSE;
      ret = GST_PAD_PROBE_REMOVE;
    } else {
      priv->blocking = FALSE;
      priv->remove_drop_probe = TRUE;
    }
  } else {
    ret = GST_PAD_PROBE_PASS;
  }
  g_mutex_unlock (&priv->lock);
  return ret;
}

static GstPadProbeReturn
rtcp_pad_blocking (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPStream *stream;
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  stream = user_data;
  priv = stream->priv;

  g_mutex_lock (&priv->lock);

  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) ||
      (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST)) {
    GST_DEBUG_OBJECT (pad, "Now blocking on buffer");
  } else if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)) {
    if (GST_EVENT_TYPE (info->data) == GST_EVENT_GAP) {
      GST_DEBUG_OBJECT (pad, "Now blocking on gap event");
      ret = GST_PAD_PROBE_OK;
    } else {
      ret = GST_PAD_PROBE_PASS;
      g_mutex_unlock (&priv->lock);
      goto done;
    }
  } else {
    g_assert_not_reached ();
  }

  g_mutex_unlock (&priv->lock);

done:
  return ret;
}

static void
install_drop_probe (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;

  priv = stream->priv;

  /* if receiver */
  if (priv->sinkpad)
    return;

  /* install for data channel only */
  if (priv->send_src[0]) {
    gst_pad_add_probe (priv->send_src[0],
        GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
        GST_PAD_PROBE_TYPE_BUFFER_LIST |
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, drop_probe,
        g_object_ref (stream), g_object_unref);
  }
}

static void
set_blocked (GstRTSPStream * stream, gboolean blocked)
{
  GstRTSPStreamPrivate *priv;
  int i;

  GST_DEBUG_OBJECT (stream, "blocked: %d", blocked);

  priv = stream->priv;

  if (blocked) {
    /* if receiver */
    if (priv->sinkpad) {
      priv->blocking = TRUE;
      return;
    }
    for (i = 0; i < 2; i++) {
      if (priv->blocked_id[i] != 0)
        continue;
      if (priv->send_src[i]) {
        priv->blocking = FALSE;
        priv->blocked_buffer = FALSE;
        priv->blocked_running_time = GST_CLOCK_TIME_NONE;
        priv->blocked_clock_rate = 0;

        if (i == 0) {
          priv->blocked_id[i] = gst_pad_add_probe (priv->send_src[i],
              GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
              GST_PAD_PROBE_TYPE_BUFFER_LIST |
              GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, rtp_pad_blocking,
              g_object_ref (stream), g_object_unref);
        } else {
          priv->blocked_id[i] = gst_pad_add_probe (priv->send_src[i],
              GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
              GST_PAD_PROBE_TYPE_BUFFER_LIST |
              GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, rtcp_pad_blocking,
              g_object_ref (stream), g_object_unref);
        }
      }
    }
  } else {
    for (i = 0; i < 2; i++) {
      if (priv->blocked_id[i] != 0) {
        gst_pad_remove_probe (priv->send_src[i], priv->blocked_id[i]);
        priv->blocked_id[i] = 0;
      }
    }
    priv->blocking = FALSE;
  }
}

/**
 * gst_rtsp_stream_set_blocked:
 * @stream: a #GstRTSPStream
 * @blocked: boolean indicating we should block or unblock
 *
 * Blocks or unblocks the dataflow on @stream.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_set_blocked (GstRTSPStream * stream, gboolean blocked)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  set_blocked (stream, blocked);
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_stream_install_drop_probe:
 * @stream: a #GstRTSPStream
 *
 * This probe can be installed when the currently blocking buffer should be
 * dropped. When it has successfully dropped the buffer, it will remove itself.
 * The goal is to avoid sending old data, typically when there has been a delay
 * between a DESCRIBE and a PLAY request.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.24
 */
gboolean
gst_rtsp_stream_install_drop_probe (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  install_drop_probe (stream);
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_stream_ublock_linked:
 * @stream: a #GstRTSPStream
 *
 * Unblocks the dataflow on @stream if it is linked.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_stream_unblock_linked (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  if (priv->send_src[0] && gst_pad_is_linked (priv->send_src[0]))
    set_blocked (stream, FALSE);
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_stream_is_blocking:
 * @stream: a #GstRTSPStream
 *
 * Check if @stream is blocking on a #GstBuffer.
 *
 * Returns: %TRUE if @stream is blocking
 */
gboolean
gst_rtsp_stream_is_blocking (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  result = priv->blocking;
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_stream_query_position:
 * @stream: a #GstRTSPStream
 * @position: (out): current position of a #GstRTSPStream
 *
 * Query the position of the stream in %GST_FORMAT_TIME. This only considers
 * the RTP parts of the pipeline and not the RTCP parts.
 *
 * Returns: %TRUE if the position could be queried
 */
gboolean
gst_rtsp_stream_query_position (GstRTSPStream * stream, gint64 * position)
{
  GstRTSPStreamPrivate *priv;
  GstElement *sink;
  GstPad *pad = NULL;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  /* query position: if no sinks have been added yet,
   * we obtain the position from the pad otherwise we query the sinks */

  priv = stream->priv;

  g_mutex_lock (&priv->lock);

  if (priv->blocking && GST_CLOCK_TIME_IS_VALID (priv->blocked_running_time)) {
    *position = priv->blocked_running_time;
    g_mutex_unlock (&priv->lock);
    return TRUE;
  }

  /* depending on the transport type, it should query corresponding sink */
  if (priv->configured_protocols & GST_RTSP_LOWER_TRANS_UDP)
    sink = priv->udpsink[0];
  else if (priv->configured_protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST)
    sink = priv->mcast_udpsink[0];
  else
    sink = priv->appsink[0];

  if (sink) {
    gst_object_ref (sink);
  } else if (priv->send_src[0]) {
    pad = gst_object_ref (priv->send_src[0]);
  } else {
    g_mutex_unlock (&priv->lock);
    GST_WARNING_OBJECT (stream, "Couldn't obtain position: erroneous pipeline");
    return FALSE;
  }
  g_mutex_unlock (&priv->lock);

  if (sink) {
    if (!gst_element_query_position (sink, GST_FORMAT_TIME, position)) {
      GST_WARNING_OBJECT (stream,
          "Couldn't obtain position: position query failed");
      gst_object_unref (sink);
      return FALSE;
    }
    gst_object_unref (sink);
  } else if (pad) {
    GstEvent *event;
    const GstSegment *segment;

    event = gst_pad_get_sticky_event (pad, GST_EVENT_SEGMENT, 0);
    if (!event) {
      GST_WARNING_OBJECT (stream, "Couldn't obtain position: no segment event");
      gst_object_unref (pad);
      return FALSE;
    }

    gst_event_parse_segment (event, &segment);
    if (segment->format != GST_FORMAT_TIME) {
      *position = -1;
    } else {
      g_mutex_lock (&priv->lock);
      *position = priv->position;
      g_mutex_unlock (&priv->lock);
      *position =
          gst_segment_to_stream_time (segment, GST_FORMAT_TIME, *position);
    }
    gst_event_unref (event);
    gst_object_unref (pad);
  }

  return TRUE;
}

/**
 * gst_rtsp_stream_query_stop:
 * @stream: a #GstRTSPStream
 * @stop: (out): current stop of a #GstRTSPStream
 *
 * Query the stop of the stream in %GST_FORMAT_TIME. This only considers
 * the RTP parts of the pipeline and not the RTCP parts.
 *
 * Returns: %TRUE if the stop could be queried
 */
gboolean
gst_rtsp_stream_query_stop (GstRTSPStream * stream, gint64 * stop)
{
  GstRTSPStreamPrivate *priv;
  GstElement *sink;
  GstPad *pad = NULL;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  /* query stop position: if no sinks have been added yet,
   * we obtain the stop position from the pad otherwise we query the sinks */

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  /* depending on the transport type, it should query corresponding sink */
  if (priv->configured_protocols & GST_RTSP_LOWER_TRANS_UDP)
    sink = priv->udpsink[0];
  else if (priv->configured_protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST)
    sink = priv->mcast_udpsink[0];
  else
    sink = priv->appsink[0];

  if (sink) {
    gst_object_ref (sink);
  } else if (priv->send_src[0]) {
    pad = gst_object_ref (priv->send_src[0]);
  } else {
    g_mutex_unlock (&priv->lock);
    GST_WARNING_OBJECT (stream, "Couldn't obtain stop: erroneous pipeline");
    return FALSE;
  }
  g_mutex_unlock (&priv->lock);

  if (sink) {
    GstQuery *query;
    GstFormat format;
    gdouble rate;
    gint64 start_value;
    gint64 stop_value;

    query = gst_query_new_segment (GST_FORMAT_TIME);
    if (!gst_element_query (sink, query)) {
      GST_WARNING_OBJECT (stream, "Couldn't obtain stop: element query failed");
      gst_query_unref (query);
      gst_object_unref (sink);
      return FALSE;
    }
    gst_query_parse_segment (query, &rate, &format, &start_value, &stop_value);
    if (format != GST_FORMAT_TIME)
      *stop = -1;
    else
      *stop = rate > 0.0 ? stop_value : start_value;
    gst_query_unref (query);
    gst_object_unref (sink);
  } else if (pad) {
    GstEvent *event;
    const GstSegment *segment;

    event = gst_pad_get_sticky_event (pad, GST_EVENT_SEGMENT, 0);
    if (!event) {
      GST_WARNING_OBJECT (stream, "Couldn't obtain stop: no segment event");
      gst_object_unref (pad);
      return FALSE;
    }
    gst_event_parse_segment (event, &segment);
    if (segment->format != GST_FORMAT_TIME) {
      *stop = -1;
    } else {
      *stop = segment->stop;
      if (*stop == -1)
        *stop = segment->duration;
      else
        *stop = gst_segment_to_stream_time (segment, GST_FORMAT_TIME, *stop);
    }
    gst_event_unref (event);
    gst_object_unref (pad);
  }

  return TRUE;
}

/**
 * gst_rtsp_stream_seekable:
 * @stream: a #GstRTSPStream
 *
 * Checks whether the individual @stream is seekable.
 *
 * Returns: %TRUE if @stream is seekable, else %FALSE.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_stream_seekable (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  GstPad *pad = NULL;
  GstQuery *query = NULL;
  gboolean seekable = FALSE;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  /* query stop position: if no sinks have been added yet,
   * we obtain the stop position from the pad otherwise we query the sinks */

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  /* depending on the transport type, it should query corresponding sink */
  if (priv->srcpad) {
    pad = gst_object_ref (priv->srcpad);
  } else {
    g_mutex_unlock (&priv->lock);
    GST_WARNING_OBJECT (stream, "Pad not available, can't query seekability");
    goto beach;
  }
  g_mutex_unlock (&priv->lock);

  query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (!gst_pad_query (pad, query)) {
    GST_WARNING_OBJECT (stream, "seeking query failed");
    goto beach;
  }
  gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);

beach:
  if (pad)
    gst_object_unref (pad);
  if (query)
    gst_query_unref (query);

  GST_DEBUG_OBJECT (stream, "Returning %d", seekable);

  return seekable;
}

/**
 * gst_rtsp_stream_complete_stream:
 * @stream: a #GstRTSPStream
 * @transport: a #GstRTSPTransport
 *
 * Add a receiver and sender part to the pipeline based on the transport from
 * SETUP.
 *
 * Returns: %TRUE if the stream has been successfully updated.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_stream_complete_stream (GstRTSPStream * stream,
    const GstRTSPTransport * transport)
{
  GstRTSPStreamPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  GST_DEBUG_OBJECT (stream, "complete stream");

  g_mutex_lock (&priv->lock);

  if (!(priv->allowed_protocols & transport->lower_transport))
    goto unallowed_transport;

  if (!create_receiver_part (stream, transport))
    goto create_receiver_error;

  /* in the RECORD case, we only add RTCP sender part */
  if (!create_sender_part (stream, transport))
    goto create_sender_error;

  priv->configured_protocols |= transport->lower_transport;

  priv->is_complete = TRUE;
  g_mutex_unlock (&priv->lock);

  GST_DEBUG_OBJECT (stream, "pipeline successfully updated");
  return TRUE;

create_receiver_error:
create_sender_error:
unallowed_transport:
  {
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
}

/**
 * gst_rtsp_stream_is_complete:
 * @stream: a #GstRTSPStream
 *
 * Checks whether the stream is complete, contains the receiver and the sender
 * parts. As the stream contains sink(s) element(s), it's possible to perform
 * seek operations on it.
 *
 * Returns: %TRUE if the stream contains at least one sink element.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_stream_is_complete (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  ret = priv->is_complete;
  g_mutex_unlock (&priv->lock);

  return ret;
}

/**
 * gst_rtsp_stream_is_sender:
 * @stream: a #GstRTSPStream
 *
 * Checks whether the stream is a sender.
 *
 * Returns: %TRUE if the stream is a sender and %FALSE otherwise.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_stream_is_sender (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  ret = (priv->srcpad != NULL);
  g_mutex_unlock (&priv->lock);

  return ret;
}

/**
 * gst_rtsp_stream_is_receiver:
 * @stream: a #GstRTSPStream
 *
 * Checks whether the stream is a receiver.
 *
 * Returns: %TRUE if the stream is a receiver and %FALSE otherwise.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_stream_is_receiver (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  ret = (priv->sinkpad != NULL);
  g_mutex_unlock (&priv->lock);

  return ret;
}

#define AES_128_KEY_LEN 16
#define AES_256_KEY_LEN 32

#define HMAC_32_KEY_LEN 4
#define HMAC_80_KEY_LEN 10

static gboolean
mikey_apply_policy (GstCaps * caps, GstMIKEYMessage * msg, guint8 policy)
{
  const gchar *srtp_cipher;
  const gchar *srtp_auth;
  const GstMIKEYPayload *sp;
  guint i;

  /* loop over Security policy until we find one containing policy */
  for (i = 0;; i++) {
    if ((sp = gst_mikey_message_find_payload (msg, GST_MIKEY_PT_SP, i)) == NULL)
      break;

    if (((GstMIKEYPayloadSP *) sp)->policy == policy)
      break;
  }

  /* the default ciphers */
  srtp_cipher = "aes-128-icm";
  srtp_auth = "hmac-sha1-80";

  /* now override the defaults with what is in the Security Policy */
  if (sp != NULL) {
    guint len;
    guint enc_alg = GST_MIKEY_ENC_AES_CM_128;

    /* collect all the params and go over them */
    len = gst_mikey_payload_sp_get_n_params (sp);
    for (i = 0; i < len; i++) {
      const GstMIKEYPayloadSPParam *param =
          gst_mikey_payload_sp_get_param (sp, i);

      switch (param->type) {
        case GST_MIKEY_SP_SRTP_ENC_ALG:
          enc_alg = param->val[0];
          switch (param->val[0]) {
            case GST_MIKEY_ENC_NULL:
              srtp_cipher = "null";
              break;
            case GST_MIKEY_ENC_AES_CM_128:
            case GST_MIKEY_ENC_AES_KW_128:
              srtp_cipher = "aes-128-icm";
              break;
            case GST_MIKEY_ENC_AES_GCM_128:
              srtp_cipher = "aes-128-gcm";
              break;
            default:
              break;
          }
          break;
        case GST_MIKEY_SP_SRTP_ENC_KEY_LEN:
          switch (param->val[0]) {
            case AES_128_KEY_LEN:
              if (enc_alg == GST_MIKEY_ENC_AES_CM_128 ||
                  enc_alg == GST_MIKEY_ENC_AES_KW_128) {
                srtp_cipher = "aes-128-icm";
              } else if (enc_alg == GST_MIKEY_ENC_AES_GCM_128) {
                srtp_cipher = "aes-128-gcm";
              }
              break;
            case AES_256_KEY_LEN:
              if (enc_alg == GST_MIKEY_ENC_AES_CM_128 ||
                  enc_alg == GST_MIKEY_ENC_AES_KW_128) {
                srtp_cipher = "aes-256-icm";
              } else if (enc_alg == GST_MIKEY_ENC_AES_GCM_128) {
                srtp_cipher = "aes-256-gcm";
              }
              break;
            default:
              break;
          }
          break;
        case GST_MIKEY_SP_SRTP_AUTH_ALG:
          switch (param->val[0]) {
            case GST_MIKEY_MAC_NULL:
              srtp_auth = "null";
              break;
            case GST_MIKEY_MAC_HMAC_SHA_1_160:
              srtp_auth = "hmac-sha1-80";
              break;
            default:
              break;
          }
          break;
        case GST_MIKEY_SP_SRTP_AUTH_KEY_LEN:
          switch (param->val[0]) {
            case HMAC_32_KEY_LEN:
              srtp_auth = "hmac-sha1-32";
              break;
            case HMAC_80_KEY_LEN:
              srtp_auth = "hmac-sha1-80";
              break;
            default:
              break;
          }
          break;
        case GST_MIKEY_SP_SRTP_SRTP_ENC:
          break;
        case GST_MIKEY_SP_SRTP_SRTCP_ENC:
          break;
        default:
          break;
      }
    }
  }
  /* now configure the SRTP parameters */
  gst_caps_set_simple (caps,
      "srtp-cipher", G_TYPE_STRING, srtp_cipher,
      "srtp-auth", G_TYPE_STRING, srtp_auth,
      "srtcp-cipher", G_TYPE_STRING, srtp_cipher,
      "srtcp-auth", G_TYPE_STRING, srtp_auth, NULL);

  return TRUE;
}

static gboolean
handle_mikey_data (GstRTSPStream * stream, guint8 * data, gsize size)
{
  GstMIKEYMessage *msg;
  guint i, n_cs;
  GstCaps *caps = NULL;
  GstMIKEYPayloadKEMAC *kemac;
  const GstMIKEYPayloadKeyData *pkd;
  GstBuffer *key;

  /* the MIKEY message contains a CSB or crypto session bundle. It is a
   * set of Crypto Sessions protected with the same master key.
   * In the context of SRTP, an RTP and its RTCP stream is part of a
   * crypto session */
  if ((msg = gst_mikey_message_new_from_data (data, size, NULL, NULL)) == NULL)
    goto parse_failed;

  /* we can only handle SRTP crypto sessions for now */
  if (msg->map_type != GST_MIKEY_MAP_TYPE_SRTP)
    goto invalid_map_type;

  /* get the number of crypto sessions. This maps SSRC to its
   * security parameters */
  n_cs = gst_mikey_message_get_n_cs (msg);
  if (n_cs == 0)
    goto no_crypto_sessions;

  /* we also need keys */
  if (!(kemac = (GstMIKEYPayloadKEMAC *) gst_mikey_message_find_payload
          (msg, GST_MIKEY_PT_KEMAC, 0)))
    goto no_keys;

  /* we don't support encrypted keys */
  if (kemac->enc_alg != GST_MIKEY_ENC_NULL
      || kemac->mac_alg != GST_MIKEY_MAC_NULL)
    goto unsupported_encryption;

  /* get Key data sub-payload */
  pkd = (const GstMIKEYPayloadKeyData *)
      gst_mikey_payload_kemac_get_sub (&kemac->pt, 0);

  key = gst_buffer_new_memdup (pkd->key_data, pkd->key_len);

  /* go over all crypto sessions and create the security policy for each
   * SSRC */
  for (i = 0; i < n_cs; i++) {
    const GstMIKEYMapSRTP *map = gst_mikey_message_get_cs_srtp (msg, i);

    caps = gst_caps_new_simple ("application/x-srtp",
        "ssrc", G_TYPE_UINT, map->ssrc,
        "roc", G_TYPE_UINT, map->roc, "srtp-key", GST_TYPE_BUFFER, key, NULL);
    mikey_apply_policy (caps, msg, map->policy);

    gst_rtsp_stream_update_crypto (stream, map->ssrc, caps);
    gst_caps_unref (caps);
  }
  gst_mikey_message_unref (msg);
  gst_buffer_unref (key);

  return TRUE;

  /* ERRORS */
parse_failed:
  {
    GST_DEBUG_OBJECT (stream, "failed to parse MIKEY message");
    return FALSE;
  }
invalid_map_type:
  {
    GST_DEBUG_OBJECT (stream, "invalid map type %d", msg->map_type);
    goto cleanup_message;
  }
no_crypto_sessions:
  {
    GST_DEBUG_OBJECT (stream, "no crypto sessions");
    goto cleanup_message;
  }
no_keys:
  {
    GST_DEBUG_OBJECT (stream, "no keys found");
    goto cleanup_message;
  }
unsupported_encryption:
  {
    GST_DEBUG_OBJECT (stream, "unsupported key encryption");
    goto cleanup_message;
  }
cleanup_message:
  {
    gst_mikey_message_unref (msg);
    return FALSE;
  }
}

#define IS_STRIP_CHAR(c) (g_ascii_isspace ((guchar)(c)) || ((c) == '\"'))

static void
strip_chars (gchar * str)
{
  gchar *s;
  gsize len;

  len = strlen (str);
  while (len--) {
    if (!IS_STRIP_CHAR (str[len]))
      break;
    str[len] = '\0';
  }
  for (s = str; *s && IS_STRIP_CHAR (*s); s++);
  memmove (str, s, len + 1);
}

/**
 * gst_rtsp_stream_handle_keymgmt:
 * @stream: a #GstRTSPStream
 * @keymgmt: a keymgmt header
 *
 * Parse and handle a KeyMgmt header.
 *
 * Since: 1.16
 */
/* KeyMgmt = "KeyMgmt" ":" key-mgmt-spec 0*("," key-mgmt-spec)
 * key-mgmt-spec = "prot" "=" KMPID ";" ["uri" "=" %x22 URI %x22 ";"]
 */
gboolean
gst_rtsp_stream_handle_keymgmt (GstRTSPStream * stream, const gchar * keymgmt)
{
  gchar **specs;
  gint i, j;

  specs = g_strsplit (keymgmt, ",", 0);
  for (i = 0; specs[i]; i++) {
    gchar **split;

    split = g_strsplit (specs[i], ";", 0);
    for (j = 0; split[j]; j++) {
      g_strstrip (split[j]);
      if (g_str_has_prefix (split[j], "prot=")) {
        g_strstrip (split[j] + 5);
        if (!g_str_equal (split[j] + 5, "mikey"))
          break;
        GST_DEBUG ("found mikey");
      } else if (g_str_has_prefix (split[j], "uri=")) {
        strip_chars (split[j] + 4);
        GST_DEBUG ("found uri '%s'", split[j] + 4);
      } else if (g_str_has_prefix (split[j], "data=")) {
        guchar *data;
        gsize size;
        strip_chars (split[j] + 5);
        GST_DEBUG ("found data '%s'", split[j] + 5);
        data = g_base64_decode_inplace (split[j] + 5, &size);
        handle_mikey_data (stream, data, size);
      }
    }
    g_strfreev (split);
  }
  g_strfreev (specs);
  return TRUE;
}


/**
 * gst_rtsp_stream_get_ulpfec_pt:
 *
 * Returns: the payload type used for ULPFEC protection packets
 *
 * Since: 1.16
 */
guint
gst_rtsp_stream_get_ulpfec_pt (GstRTSPStream * stream)
{
  guint res;

  g_mutex_lock (&stream->priv->lock);
  res = stream->priv->ulpfec_pt;
  g_mutex_unlock (&stream->priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_set_ulpfec_pt:
 *
 * Set the payload type to be used for ULPFEC protection packets
 *
 * Since: 1.16
 */
void
gst_rtsp_stream_set_ulpfec_pt (GstRTSPStream * stream, guint pt)
{
  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  g_mutex_lock (&stream->priv->lock);
  stream->priv->ulpfec_pt = pt;
  if (stream->priv->ulpfec_encoder) {
    g_object_set (stream->priv->ulpfec_encoder, "pt", pt, NULL);
  }
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_request_ulpfec_decoder:
 *
 * Creating a rtpulpfecdec element
 *
 * Returns: (transfer full) (nullable): a #GstElement.
 *
 * Since: 1.16
 */
GstElement *
gst_rtsp_stream_request_ulpfec_decoder (GstRTSPStream * stream,
    GstElement * rtpbin, guint sessid)
{
  GObject *internal_storage = NULL;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  stream->priv->ulpfec_decoder =
      gst_object_ref (gst_element_factory_make ("rtpulpfecdec", NULL));

  g_signal_emit_by_name (G_OBJECT (rtpbin), "get-internal-storage", sessid,
      &internal_storage);
  g_object_set (stream->priv->ulpfec_decoder, "storage", internal_storage,
      NULL);
  g_object_unref (internal_storage);
  update_ulpfec_decoder_pt (stream);

  return stream->priv->ulpfec_decoder;
}

/**
 * gst_rtsp_stream_request_ulpfec_encoder:
 *
 * Creating a rtpulpfecenc element
 *
 * Returns: (transfer full) (nullable): a #GstElement.
 *
 * Since: 1.16
 */
GstElement *
gst_rtsp_stream_request_ulpfec_encoder (GstRTSPStream * stream, guint sessid)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);

  if (!stream->priv->ulpfec_percentage)
    return NULL;

  stream->priv->ulpfec_encoder =
      gst_object_ref (gst_element_factory_make ("rtpulpfecenc", NULL));

  g_object_set (stream->priv->ulpfec_encoder, "pt", stream->priv->ulpfec_pt,
      "percentage", stream->priv->ulpfec_percentage, NULL);

  return stream->priv->ulpfec_encoder;
}

/**
 * gst_rtsp_stream_set_ulpfec_percentage:
 *
 * Sets the amount of redundancy to apply when creating ULPFEC
 * protection packets.
 *
 * Since: 1.16
 */
void
gst_rtsp_stream_set_ulpfec_percentage (GstRTSPStream * stream, guint percentage)
{
  g_return_if_fail (GST_IS_RTSP_STREAM (stream));

  g_mutex_lock (&stream->priv->lock);
  stream->priv->ulpfec_percentage = percentage;
  if (stream->priv->ulpfec_encoder) {
    g_object_set (stream->priv->ulpfec_encoder, "percentage", percentage, NULL);
  }
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_get_ulpfec_percentage:
 *
 * Returns: the amount of redundancy applied when creating ULPFEC
 * protection packets.
 *
 * Since: 1.16
 */
guint
gst_rtsp_stream_get_ulpfec_percentage (GstRTSPStream * stream)
{
  guint res;

  g_mutex_lock (&stream->priv->lock);
  res = stream->priv->ulpfec_percentage;
  g_mutex_unlock (&stream->priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_set_rate_control:
 *
 * Define whether @stream will follow the Rate-Control=no behaviour as specified
 * in the ONVIF replay spec.
 *
 * Since: 1.18
 */
void
gst_rtsp_stream_set_rate_control (GstRTSPStream * stream, gboolean enabled)
{
  GST_DEBUG_OBJECT (stream, "%s rate control",
      enabled ? "Enabling" : "Disabling");

  g_mutex_lock (&stream->priv->lock);
  stream->priv->do_rate_control = enabled;
  if (stream->priv->appsink[0])
    g_object_set (stream->priv->appsink[0], "sync", enabled, NULL);
  if (stream->priv->payloader
      && g_object_class_find_property (G_OBJECT_GET_CLASS (stream->
              priv->payloader), "onvif-no-rate-control"))
    g_object_set (stream->priv->payloader, "onvif-no-rate-control", !enabled,
        NULL);
  if (stream->priv->session) {
    g_object_set (stream->priv->session, "disable-sr-timestamp", !enabled,
        NULL);
  }
  g_mutex_unlock (&stream->priv->lock);
}

/**
 * gst_rtsp_stream_get_rate_control:
 *
 * Returns: whether @stream will follow the Rate-Control=no behaviour as specified
 * in the ONVIF replay spec.
 *
 * Since: 1.18
 */
gboolean
gst_rtsp_stream_get_rate_control (GstRTSPStream * stream)
{
  gboolean ret;

  g_mutex_lock (&stream->priv->lock);
  ret = stream->priv->do_rate_control;
  g_mutex_unlock (&stream->priv->lock);

  return ret;
}

/**
 * gst_rtsp_stream_unblock_rtcp:
 *
 * Remove blocking probe from the RTCP source. When creating an UDP source for
 * RTCP it is initially blocked until this function is called.
 * This functions should be called once the pipeline is ready for handling RTCP
 * packets.
 *
 * Since: 1.20
 */
void
gst_rtsp_stream_unblock_rtcp (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv;

  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  if (priv->block_early_rtcp_probe != 0) {
    gst_pad_remove_probe
        (priv->block_early_rtcp_pad, priv->block_early_rtcp_probe);
    priv->block_early_rtcp_probe = 0;
    gst_object_unref (priv->block_early_rtcp_pad);
    priv->block_early_rtcp_pad = NULL;
  }
  if (priv->block_early_rtcp_probe_ipv6 != 0) {
    gst_pad_remove_probe
        (priv->block_early_rtcp_pad_ipv6, priv->block_early_rtcp_probe_ipv6);
    priv->block_early_rtcp_probe_ipv6 = 0;
    gst_object_unref (priv->block_early_rtcp_pad_ipv6);
    priv->block_early_rtcp_pad_ipv6 = NULL;
  }
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_stream_set_drop_delta_units:
 * @stream: a #GstRTSPStream
 * @drop: TRUE if delta unit frames are supposed to be dropped.
 *
 * Decide whether the blocking probe is supposed to drop delta units at the
 * beginning of a stream.
 *
 * Since: 1.24
 */
void
gst_rtsp_stream_set_drop_delta_units (GstRTSPStream * stream, gboolean drop)
{
  GstRTSPStreamPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM (stream));
  priv = stream->priv;
  g_mutex_lock (&priv->lock);
  priv->drop_delta_units = drop;
  g_mutex_unlock (&priv->lock);
}
