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
 * Last reviewed on 2013-07-16 (1.0.0)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gio/gio.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "rtsp-stream.h"

#define GST_RTSP_STREAM_GET_PRIVATE(obj)  \
     (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_STREAM, GstRTSPStreamPrivate))

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

  GstRTSPProfile profiles;
  GstRTSPLowerTrans protocols;

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

  /* for UDP multicast */
  GstElement *mcast_udpsrc_v4[2];
  GstElement *mcast_udpsrc_v6[2];
  GstElement *mcast_udpqueue[2];
  GstElement *mcast_udpsink[2];

  /* for TCP transport */
  GstElement *appsrc[2];
  GstClockTime appsrc_base_time[2];
  GstElement *appqueue[2];
  GstElement *appsink[2];

  GstElement *tee[2];
  GstElement *funnel[2];

  /* retransmission */
  GstElement *rtxsend;
  guint rtx_pt;
  GstClockTime rtx_time;

  /* pool used to manage unicast and multicast addresses */
  GstRTSPAddressPool *pool;

  /* unicast server addr/port */
  GstRTSPAddress *server_addr_v4;
  GstRTSPAddress *server_addr_v6;

  /* multicast addresses */
  GstRTSPAddress *mcast_addr_v4;
  GstRTSPAddress *mcast_addr_v6;

  gchar *multicast_iface;

  /* the caps of the stream */
  gulong caps_sig;
  GstCaps *caps;

  /* transports we stream to */
  guint n_active;
  GList *transports;
  guint transports_cookie;
  GList *tr_cache_rtp;
  GList *tr_cache_rtcp;
  guint tr_cache_cookie_rtp;
  guint tr_cache_cookie_rtcp;

  gint dscp_qos;

  /* stream blocking */
  gulong blocked_id[2];
  gboolean blocking;

  /* pt->caps map for RECORD streams */
  GHashTable *ptmap;

  GstRTSPPublishClockMode publish_clock_mode;
};

#define DEFAULT_CONTROL         NULL
#define DEFAULT_PROFILES        GST_RTSP_PROFILE_AVP
#define DEFAULT_PROTOCOLS       GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | \
                                        GST_RTSP_LOWER_TRANS_TCP

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

static guint gst_rtsp_stream_signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (GstRTSPStream, gst_rtsp_stream, G_TYPE_OBJECT);

static void
gst_rtsp_stream_class_init (GstRTSPStreamClass * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (GstRTSPStreamPrivate));

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
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  gst_rtsp_stream_signals[SIGNAL_NEW_RTCP_ENCODER] =
      g_signal_new ("new-rtcp-encoder", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (rtsp_stream_debug, "rtspstream", 0, "GstRTSPStream");

  ssrc_stream_map_key = g_quark_from_static_string ("GstRTSPServer.stream");
}

static void
gst_rtsp_stream_init (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = GST_RTSP_STREAM_GET_PRIVATE (stream);

  GST_DEBUG ("new stream %p", stream);

  stream->priv = priv;

  priv->dscp_qos = -1;
  priv->control = g_strdup (DEFAULT_CONTROL);
  priv->profiles = DEFAULT_PROFILES;
  priv->protocols = DEFAULT_PROTOCOLS;
  priv->publish_clock_mode = GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK;

  g_mutex_init (&priv->lock);

  priv->keys = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_caps_unref);
  priv->ptmap = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gst_caps_unref);
}

static void
gst_rtsp_stream_finalize (GObject * obj)
{
  GstRTSPStream *stream;
  GstRTSPStreamPrivate *priv;

  stream = GST_RTSP_STREAM (obj);
  priv = stream->priv;

  GST_DEBUG ("finalize stream %p", stream);

  /* we really need to be unjoined now */
  g_return_if_fail (priv->joined_bin == NULL);

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

  g_free (priv->multicast_iface);

  gst_object_unref (priv->payloader);
  if (priv->srcpad)
    gst_object_unref (priv->srcpad);
  if (priv->sinkpad)
    gst_object_unref (priv->sinkpad);
  g_free (priv->control);
  g_mutex_clear (&priv->lock);

  g_hash_table_unref (priv->keys);
  g_hash_table_destroy (priv->ptmap);

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
 * Returns: (transfer full): the srcpad. Unref after usage.
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
 * Returns: (transfer full): the sinkpad. Unref after usage.
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
 * Returns: (transfer full): the control string. g_free() after usage.
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
 * @control: a control string
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
 * @control: a control string
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
update_dscp_qos (GstRTSPStream * stream, GstElement * udpsink[2])
{
  GstRTSPStreamPrivate *priv;

  priv = stream->priv;

  if (udpsink[0]) {
    g_object_set (G_OBJECT (udpsink[0]), "qos-dscp", priv->dscp_qos, NULL);
  }

  if (udpsink[1]) {
    g_object_set (G_OBJECT (udpsink[1]), "qos-dscp", priv->dscp_qos, NULL);
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

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if (transport->trans != GST_RTSP_TRANS_RTP)
    goto unsupported_transmode;

  if (!(transport->profile & priv->profiles))
    goto unsupported_profile;

  if (!(transport->lower_transport & priv->protocols))
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
  priv->protocols = protocols;
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
  res = priv->protocols;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_stream_set_address_pool:
 * @stream: a #GstRTSPStream
 * @pool: (transfer none): a #GstRTSPAddressPool
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
 * Returns: (transfer full): the #GstRTSPAddressPool of @stream. g_object_unref() after
 * usage.
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
 * @multicast_iface: (transfer none): a multicast interface name
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
 * Returns: (transfer full): the multicast interface for @stream. g_free() after
 * usage.
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
 * the address could be reserved. gst_rtsp_address_free() after usage.
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
set_sockets_for_udpsinks (GstElement * udpsink[2], GSocket * rtp_socket,
    GSocket * rtcp_socket, GSocketFamily family)
{
  const gchar *multisink_socket;

  if (family == G_SOCKET_FAMILY_IPV6)
    multisink_socket = "socket-v6";
  else
    multisink_socket = "socket";

  g_object_set (G_OBJECT (udpsink[0]), multisink_socket, rtp_socket, NULL);
  g_object_set (G_OBJECT (udpsink[1]), multisink_socket, rtcp_socket, NULL);
}

static gboolean
create_and_configure_udpsinks (GstRTSPStream * stream, GstElement * udpsink[2])
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GstElement *udpsink0, *udpsink1;

  udpsink0 = gst_element_factory_make ("multiudpsink", NULL);
  udpsink1 = gst_element_factory_make ("multiudpsink", NULL);

  if (!udpsink0 || !udpsink1)
    goto no_udp_protocol;

  /* configure sinks */

  g_object_set (G_OBJECT (udpsink0), "close-socket", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "close-socket", FALSE, NULL);

  g_object_set (G_OBJECT (udpsink0), "send-duplicates", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "send-duplicates", FALSE, NULL);

  g_object_set (G_OBJECT (udpsink0), "buffer-size", priv->buffer_size, NULL);

  g_object_set (G_OBJECT (udpsink1), "sync", FALSE, NULL);
  /* Needs to be async for RECORD streams, otherwise we will never go to
   * PLAYING because the sinks will wait for data while the udpsrc can't
   * provide data with timestamps in PAUSED. */
  if (priv->sinkpad)
    g_object_set (G_OBJECT (udpsink0), "async", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "async", FALSE, NULL);

  /* join multicast group when adding clients, so we'll start receiving from it.
   * We cannot rely on the udpsrc to join the group since its socket is always a
   * local unicast one. */
  g_object_set (G_OBJECT (udpsink0), "auto-multicast", TRUE, NULL);
  g_object_set (G_OBJECT (udpsink1), "auto-multicast", TRUE, NULL);

  g_object_set (G_OBJECT (udpsink0), "loop", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "loop", FALSE, NULL);

  udpsink[0] = udpsink0;
  udpsink[1] = udpsink1;

  /* update the dscp qos field in the sinks */
  update_dscp_qos (stream, udpsink);

  return TRUE;

  /* ERRORS */
no_udp_protocol:
  {
    return FALSE;
  }
}

/* must be called with lock */
static gboolean
create_and_configure_udpsources (GstElement * udpsrc_out[2],
    GSocket * rtp_socket, GSocket * rtcp_socket)
{
  GstStateChangeReturn ret;

  udpsrc_out[0] = gst_element_factory_make ("udpsrc", NULL);
  udpsrc_out[1] = gst_element_factory_make ("udpsrc", NULL);

  if (udpsrc_out[0] == NULL || udpsrc_out[1] == NULL)
    goto error;

  g_object_set (G_OBJECT (udpsrc_out[0]), "socket", rtp_socket, NULL);
  g_object_set (G_OBJECT (udpsrc_out[1]), "socket", rtcp_socket, NULL);

  /* The udpsrc cannot do the join because its socket is always a local unicast
   * one. The udpsink sharing the same socket will do it for us. */
  g_object_set (G_OBJECT (udpsrc_out[0]), "auto-multicast", FALSE, NULL);
  g_object_set (G_OBJECT (udpsrc_out[1]), "auto-multicast", FALSE, NULL);

  g_object_set (G_OBJECT (udpsrc_out[0]), "loop", FALSE, NULL);
  g_object_set (G_OBJECT (udpsrc_out[1]), "loop", FALSE, NULL);

  g_object_set (G_OBJECT (udpsrc_out[0]), "close-socket", FALSE, NULL);
  g_object_set (G_OBJECT (udpsrc_out[1]), "close-socket", FALSE, NULL);

  ret = gst_element_set_state (udpsrc_out[0], GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto error;
  ret = gst_element_set_state (udpsrc_out[1], GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto error;

  return TRUE;

  /* ERRORS */
error:
  {
    if (udpsrc_out[0]) {
      gst_element_set_state (udpsrc_out[0], GST_STATE_NULL);
      g_clear_object (&udpsrc_out[0]);
    }
    if (udpsrc_out[1]) {
      gst_element_set_state (udpsrc_out[1], GST_STATE_NULL);
      g_clear_object (&udpsrc_out[1]);
    }
    return FALSE;
  }
}

static gboolean
alloc_ports_one_family (GstRTSPStream * stream, GSocketFamily family,
    GstElement * udpsrc_out[2], GstElement * udpsink_out[2],
    GstRTSPAddress ** server_addr_out, gboolean multicast)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GSocket *rtp_socket = NULL;
  GSocket *rtcp_socket;
  gint tmp_rtp, tmp_rtcp;
  guint count;
  gint rtpport, rtcpport;
  GList *rejected_addresses = NULL;
  GstRTSPAddress *addr = NULL;
  GInetAddress *inetaddr = NULL;
  gchar *addr_str;
  GSocketAddress *rtp_sockaddr = NULL;
  GSocketAddress *rtcp_sockaddr = NULL;
  GstRTSPAddressPool *pool;

  g_assert (!udpsrc_out[0]);
  g_assert (!udpsrc_out[1]);
  g_assert ((!udpsink_out[0] && !udpsink_out[1]) ||
      (udpsink_out[0] && udpsink_out[1]));
  g_assert (*server_addr_out == NULL);

  pool = priv->pool;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;

  rtcp_socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, NULL);
  if (!rtcp_socket)
    goto no_udp_protocol;
  g_socket_set_multicast_loopback (rtcp_socket, FALSE);

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:

  if (rtp_socket == NULL) {
    rtp_socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
        G_SOCKET_PROTOCOL_UDP, NULL);
    if (!rtp_socket)
      goto no_udp_protocol;
    g_socket_set_multicast_loopback (rtp_socket, FALSE);
  }

  if ((pool && gst_rtsp_address_pool_has_unicast_addresses (pool)) || multicast) {
    GstRTSPAddressFlags flags;

    if (addr)
      rejected_addresses = g_list_prepend (rejected_addresses, addr);

    if (!pool)
      goto no_ports;

    flags = GST_RTSP_ADDRESS_FLAG_EVEN_PORT;
    if (multicast)
      flags |= GST_RTSP_ADDRESS_FLAG_MULTICAST;
    else
      flags |= GST_RTSP_ADDRESS_FLAG_UNICAST;

    if (family == G_SOCKET_FAMILY_IPV6)
      flags |= GST_RTSP_ADDRESS_FLAG_IPV6;
    else
      flags |= GST_RTSP_ADDRESS_FLAG_IPV4;

    addr = gst_rtsp_address_pool_acquire_address (pool, flags, 2);

    if (addr == NULL)
      goto no_ports;

    tmp_rtp = addr->port;

    g_clear_object (&inetaddr);
    if (multicast)
      inetaddr = g_inet_address_new_any (family);
    else
      inetaddr = g_inet_address_new_from_string (addr->address);
  } else {
    if (tmp_rtp != 0) {
      tmp_rtp += 2;
      if (++count > 20)
        goto no_ports;
    }

    if (inetaddr == NULL)
      inetaddr = g_inet_address_new_any (family);
  }

  rtp_sockaddr = g_inet_socket_address_new (inetaddr, tmp_rtp);
  if (!g_socket_bind (rtp_socket, rtp_sockaddr, FALSE, NULL)) {
    g_object_unref (rtp_sockaddr);
    goto again;
  }
  g_object_unref (rtp_sockaddr);

  rtp_sockaddr = g_socket_get_local_address (rtp_socket, NULL);
  if (rtp_sockaddr == NULL || !G_IS_INET_SOCKET_ADDRESS (rtp_sockaddr)) {
    g_clear_object (&rtp_sockaddr);
    goto socket_error;
  }

  tmp_rtp =
      g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (rtp_sockaddr));
  g_object_unref (rtp_sockaddr);

  /* check if port is even */
  if ((tmp_rtp & 1) != 0) {
    /* port not even, close and allocate another */
    tmp_rtp++;
    g_clear_object (&rtp_socket);
    goto again;
  }

  /* set port */
  tmp_rtcp = tmp_rtp + 1;

  rtcp_sockaddr = g_inet_socket_address_new (inetaddr, tmp_rtcp);
  if (!g_socket_bind (rtcp_socket, rtcp_sockaddr, FALSE, NULL)) {
    g_object_unref (rtcp_sockaddr);
    g_clear_object (&rtp_socket);
    goto again;
  }
  g_object_unref (rtcp_sockaddr);

  if (!addr) {
    addr = g_slice_new0 (GstRTSPAddress);
    addr->address = g_inet_address_to_string (inetaddr);
    addr->port = tmp_rtp;
    addr->n_ports = 2;
  }

  addr_str = addr->address;
  g_clear_object (&inetaddr);

  if (!create_and_configure_udpsources (udpsrc_out, rtp_socket, rtcp_socket)) {
    goto no_udp_protocol;
  }

  g_object_get (G_OBJECT (udpsrc_out[0]), "port", &rtpport, NULL);
  g_object_get (G_OBJECT (udpsrc_out[1]), "port", &rtcpport, NULL);

  /* this should not happen... */
  if (rtpport != tmp_rtp || rtcpport != tmp_rtcp)
    goto port_error;

  /* This function is called twice (for v4 and v6) but we create only one pair
   * of udpsinks. */
  if (!udpsink_out[0]
      && !create_and_configure_udpsinks (stream, udpsink_out))
    goto no_udp_protocol;

  if (multicast) {
    g_object_set (G_OBJECT (udpsink_out[0]), "multicast-iface",
        priv->multicast_iface, NULL);
    g_object_set (G_OBJECT (udpsink_out[1]), "multicast-iface",
        priv->multicast_iface, NULL);

    g_signal_emit_by_name (udpsink_out[0], "add", addr_str, rtpport, NULL);
    g_signal_emit_by_name (udpsink_out[1], "add", addr_str, rtcpport, NULL);
  }

  set_sockets_for_udpsinks (udpsink_out, rtp_socket, rtcp_socket, family);

  *server_addr_out = addr;

  g_list_free_full (rejected_addresses, (GDestroyNotify) gst_rtsp_address_free);

  g_object_unref (rtp_socket);
  g_object_unref (rtcp_socket);

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
port_error:
  {
    goto cleanup;
  }
socket_error:
  {
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

/**
 * gst_rtsp_stream_allocate_udp_sockets:
 * @stream: a #GstRTSPStream
 * @family: protocol family
 * @transport: transport method
 * @use_client_setttings: Whether to use client settings or not
 *
 * Allocates RTP and RTCP ports.
 *
 * Returns: %TRUE if the RTP and RTCP sockets have been succeccully allocated.
 * Deprecated: This function shouldn't have been made public
 */
gboolean
gst_rtsp_stream_allocate_udp_sockets (GstRTSPStream * stream,
    GSocketFamily family, GstRTSPTransport * ct, gboolean use_client_settings)
{
  g_warn_if_reached ();
  return FALSE;
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

/* must be called with lock */
static gboolean
alloc_ports (GstRTSPStream * stream)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  gboolean ret = TRUE;

  if (priv->protocols & GST_RTSP_LOWER_TRANS_UDP) {
    ret = alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV4,
        priv->udpsrc_v4, priv->udpsink, &priv->server_addr_v4, FALSE);

    ret |= alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV6,
        priv->udpsrc_v6, priv->udpsink, &priv->server_addr_v6, FALSE);
  }

  /* FIXME: Maybe actually consider the return values? */
  if (priv->protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    ret |= alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV4,
        priv->mcast_udpsrc_v4, priv->mcast_udpsink, &priv->mcast_addr_v4, TRUE);

    ret |= alloc_ports_one_family (stream, G_SOCKET_FAMILY_IPV6,
        priv->mcast_udpsrc_v6, priv->mcast_udpsink, &priv->mcast_addr_v6, TRUE);
  }

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
      server_port->max =
          priv->server_addr_v4->port + priv->server_addr_v4->n_ports - 1;
    }
  } else {
    if (server_port && priv->server_addr_v6) {
      server_port->min = priv->server_addr_v6->port;
      server_port->max =
          priv->server_addr_v6->port + priv->server_addr_v6->n_ports - 1;
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
 * Returns: (transfer full): The RTP session of this stream. Unref after usage.
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
 * Returns: (transfer full): The SRTP encoder for this stream. Unref after usage.
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
clear_tr_cache (GstRTSPStreamPrivate * priv, gboolean is_rtp)
{
  if (is_rtp) {
    g_list_foreach (priv->tr_cache_rtp, (GFunc) g_object_unref, NULL);
    g_list_free (priv->tr_cache_rtp);
    priv->tr_cache_rtp = NULL;
  } else {
    g_list_foreach (priv->tr_cache_rtcp, (GFunc) g_object_unref, NULL);
    g_list_free (priv->tr_cache_rtcp);
    priv->tr_cache_rtcp = NULL;
  }
}

static GstFlowReturn
handle_new_sample (GstAppSink * sink, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  GList *walk;
  GstSample *sample;
  GstBuffer *buffer;
  GstRTSPStream *stream;
  gboolean is_rtp;

  sample = gst_app_sink_pull_sample (sink);
  if (!sample)
    return GST_FLOW_OK;

  stream = (GstRTSPStream *) user_data;
  priv = stream->priv;
  buffer = gst_sample_get_buffer (sample);

  is_rtp = GST_ELEMENT_CAST (sink) == priv->appsink[0];

  g_mutex_lock (&priv->lock);
  if (is_rtp) {
    if (priv->tr_cache_cookie_rtp != priv->transports_cookie) {
      clear_tr_cache (priv, is_rtp);
      for (walk = priv->transports; walk; walk = g_list_next (walk)) {
        GstRTSPStreamTransport *tr = (GstRTSPStreamTransport *) walk->data;
        priv->tr_cache_rtp =
            g_list_prepend (priv->tr_cache_rtp, g_object_ref (tr));
      }
      priv->tr_cache_cookie_rtp = priv->transports_cookie;
    }
  } else {
    if (priv->tr_cache_cookie_rtcp != priv->transports_cookie) {
      clear_tr_cache (priv, is_rtp);
      for (walk = priv->transports; walk; walk = g_list_next (walk)) {
        GstRTSPStreamTransport *tr = (GstRTSPStreamTransport *) walk->data;
        priv->tr_cache_rtcp =
            g_list_prepend (priv->tr_cache_rtcp, g_object_ref (tr));
      }
      priv->tr_cache_cookie_rtcp = priv->transports_cookie;
    }
  }
  g_mutex_unlock (&priv->lock);

  if (is_rtp) {
    for (walk = priv->tr_cache_rtp; walk; walk = g_list_next (walk)) {
      GstRTSPStreamTransport *tr = (GstRTSPStreamTransport *) walk->data;
      gst_rtsp_stream_transport_send_rtp (tr, buffer);
    }
  } else {
    for (walk = priv->tr_cache_rtcp; walk; walk = g_list_next (walk)) {
      GstRTSPStreamTransport *tr = (GstRTSPStreamTransport *) walk->data;
      gst_rtsp_stream_transport_send_rtcp (tr, buffer);
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
  pad = gst_element_get_request_pad (enc, name);
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
  pad = gst_element_get_request_pad (enc, name);
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
 * Returns: (transfer full): a #GstElement.
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

  g_mutex_lock (&priv->lock);
  g_hash_table_insert (priv->ptmap, GINT_TO_POINTER (pt), gst_caps_ref (caps));
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

static void
plug_sink (GstBin * bin, GstElement * tee, GstElement * sink,
    GstElement ** queue_out)
{
  GstPad *pad;
  GstPad *teepad;
  GstPad *queuepad;

  gst_bin_add (bin, sink);

  *queue_out = gst_element_factory_make ("queue", NULL);
  g_object_set (*queue_out, "max-size-buffers", 1, "max-size-bytes", 0,
      "max-size-time", G_GINT64_CONSTANT (0), NULL);
  gst_bin_add (bin, *queue_out);

  /* link tee to queue */
  teepad = gst_element_get_request_pad (tee, "src_%u");
  pad = gst_element_get_static_pad (*queue_out, "sink");
  gst_pad_link (teepad, pad);
  gst_object_unref (pad);
  gst_object_unref (teepad);

  /* link queue to sink */
  queuepad = gst_element_get_static_pad (*queue_out, "src");
  pad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (queuepad, pad);
  gst_object_unref (queuepad);
  gst_object_unref (pad);
}

/* must be called with lock */
static void
create_sender_part (GstRTSPStream * stream, GstBin * bin, GstState state)
{
  GstRTSPStreamPrivate *priv;
  GstPad *pad;
  gboolean is_tcp, is_udp;
  gint i;

  priv = stream->priv;

  is_tcp = priv->protocols & GST_RTSP_LOWER_TRANS_TCP;
  is_udp = ((priv->protocols & GST_RTSP_LOWER_TRANS_UDP) ||
      (priv->protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST));

  for (i = 0; i < 2; i++) {
    /* For the sender we create this bit of pipeline for both
     * RTP and RTCP. Sync and preroll are enabled on udpsink so
     * we need to add a queue before appsink and udpsink to make
     * the pipeline not block. For the TCP case, we want to pump
     * client as fast as possible anyway. This pipeline is used
     * when both TCP and UDP are present.
     *
     * .--------.      .-----.    .---------.    .---------.
     * | rtpbin |      | tee |    |  queue  |    | udpsink |
     * |       send->sink   src->sink      src->sink       |
     * '--------'      |     |    '---------'    '---------'
     *                 |     |    .---------.    .---------.
     *                 |     |    |  queue  |    | appsink |
     *                 |    src->sink      src->sink       |
     *                 '-----'    '---------'    '---------'
     *
     * When only UDP or only TCP is allowed, we skip the tee and queue
     * and link the udpsink (for UDP) or appsink (for TCP) directly to
     * the session.
     */

    /* Only link the RTP send src if we're going to send RTP, link
     * the RTCP send src always */
    if (!priv->srcpad && i == 0)
      continue;

    if (is_tcp) {
      /* make appsink */
      priv->appsink[i] = gst_element_factory_make ("appsink", NULL);
      g_object_set (priv->appsink[i], "emit-signals", FALSE, NULL);
      gst_app_sink_set_callbacks (GST_APP_SINK_CAST (priv->appsink[i]),
          &sink_cb, stream, NULL);
    }

    /* If we have udp always use a tee because we could have mcast clients
     * requesting different ports, in which case we'll have to plug more
     * udpsinks. */
    if (is_udp) {
      /* make tee for RTP/RTCP */
      priv->tee[i] = gst_element_factory_make ("tee", NULL);
      gst_bin_add (bin, priv->tee[i]);

      /* and link to rtpbin send pad */
      pad = gst_element_get_static_pad (priv->tee[i], "sink");
      gst_pad_link (priv->send_src[i], pad);
      gst_object_unref (pad);

      if (priv->udpsink[i])
        plug_sink (bin, priv->tee[i], priv->udpsink[i], &priv->udpqueue[i]);

      if (priv->mcast_udpsink[i])
        plug_sink (bin, priv->tee[i], priv->mcast_udpsink[i],
            &priv->mcast_udpqueue[i]);

      if (is_tcp) {
        g_object_set (priv->appsink[i], "async", FALSE, "sync", FALSE, NULL);
        plug_sink (bin, priv->tee[i], priv->appsink[i], &priv->appqueue[i]);
      }
    } else if (is_tcp) {
      /* only appsink needed, link it to the session */
      gst_bin_add (bin, priv->appsink[i]);
      pad = gst_element_get_static_pad (priv->appsink[i], "sink");
      gst_pad_link (priv->send_src[i], pad);
      gst_object_unref (pad);

      /* when its only TCP, we need to set sync and preroll to FALSE
       * for the sink to avoid deadlock. And this is only needed for
       * sink used for RTCP data, not the RTP data. */
      if (i == 1)
        g_object_set (priv->appsink[i], "async", FALSE, "sync", FALSE, NULL);
    }

    /* check if we need to set to a special state */
    if (state != GST_STATE_NULL) {
      if (priv->udpsink[i])
        gst_element_set_state (priv->udpsink[i], state);
      if (priv->mcast_udpsink[i])
        gst_element_set_state (priv->mcast_udpsink[i], state);
      if (priv->appsink[i])
        gst_element_set_state (priv->appsink[i], state);
      if (priv->appqueue[i])
        gst_element_set_state (priv->appqueue[i], state);
      if (priv->udpqueue[i])
        gst_element_set_state (priv->udpqueue[i], state);
      if (priv->mcast_udpqueue[i])
        gst_element_set_state (priv->mcast_udpqueue[i], state);
      if (priv->tee[i])
        gst_element_set_state (priv->tee[i], state);
    }
  }
}

/* must be called with lock */
static void
plug_src (GstRTSPStream * stream, GstBin * bin, GstElement * src,
    GstElement * funnel)
{
  GstRTSPStreamPrivate *priv;
  GstPad *pad, *selpad;

  priv = stream->priv;

  if (priv->srcpad) {
    /* we set and keep these to playing so that they don't cause NO_PREROLL return
     * values. This is only relevant for PLAY pipelines */
    gst_element_set_state (src, GST_STATE_PLAYING);
    gst_element_set_locked_state (src, TRUE);
  }

  /* add src */
  gst_bin_add (bin, src);

  /* and link to the funnel */
  selpad = gst_element_get_request_pad (funnel, "sink_%u");
  pad = gst_element_get_static_pad (src, "src");
  gst_pad_link (pad, selpad);
  gst_object_unref (pad);
  gst_object_unref (selpad);
}

/* must be called with lock */
static void
create_receiver_part (GstRTSPStream * stream, GstBin * bin, GstState state)
{
  GstRTSPStreamPrivate *priv;
  GstPad *pad;
  gboolean is_tcp;
  gint i;

  priv = stream->priv;

  is_tcp = priv->protocols & GST_RTSP_LOWER_TRANS_TCP;

  for (i = 0; i < 2; i++) {
    /* For the receiver we create this bit of pipeline for both
     * RTP and RTCP. We receive RTP/RTCP on appsrc and udpsrc
     * and it is all funneled into the rtpbin receive pad.
     *
     * .--------.     .--------.    .--------.
     * | udpsrc |     | funnel |    | rtpbin |
     * |       src->sink      src->sink      |
     * '--------'     |        |    '--------'
     * .--------.     |        |
     * | appsrc |     |        |
     * |       src->sink       |
     * '--------'     '--------'
     */

    if (!priv->sinkpad && i == 0) {
      /* Only connect recv RTP sink if we expect to receive RTP. Connect recv
       * RTCP sink always */
      continue;
    }

    /* make funnel for the RTP/RTCP receivers */
    priv->funnel[i] = gst_element_factory_make ("funnel", NULL);
    gst_bin_add (bin, priv->funnel[i]);

    pad = gst_element_get_static_pad (priv->funnel[i], "src");
    gst_pad_link (pad, priv->recv_sink[i]);
    gst_object_unref (pad);

    if (priv->udpsrc_v4[i])
      plug_src (stream, bin, priv->udpsrc_v4[i], priv->funnel[i]);

    if (priv->udpsrc_v6[i])
      plug_src (stream, bin, priv->udpsrc_v6[i], priv->funnel[i]);

    if (priv->mcast_udpsrc_v4[i])
      plug_src (stream, bin, priv->mcast_udpsrc_v4[i], priv->funnel[i]);

    if (priv->mcast_udpsrc_v6[i])
      plug_src (stream, bin, priv->mcast_udpsrc_v6[i], priv->funnel[i]);

    if (is_tcp) {
      /* make and add appsrc */
      priv->appsrc[i] = gst_element_factory_make ("appsrc", NULL);
      priv->appsrc_base_time[i] = -1;
      g_object_set (priv->appsrc[i], "format", GST_FORMAT_TIME, "is-live",
          TRUE, NULL);
      plug_src (stream, bin, priv->appsrc[i], priv->funnel[i]);
    }

    /* check if we need to set to a special state */
    if (state != GST_STATE_NULL) {
      gst_element_set_state (priv->funnel[i], state);
    }
  }
}

static gboolean
check_mcast_part_for_transport (GstRTSPStream * stream,
    const GstRTSPTransport * tr)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  GInetAddress *inetaddr;
  GSocketFamily family;
  GstRTSPAddress *mcast_addr;

  /* Check if it's a ipv4 or ipv6 transport */
  inetaddr = g_inet_address_new_from_string (tr->destination);
  family = g_inet_address_get_family (inetaddr);
  g_object_unref (inetaddr);

  /* Select fields corresponding to the family */
  if (family == G_SOCKET_FAMILY_IPV4) {
    mcast_addr = priv->mcast_addr_v4;
  } else {
    mcast_addr = priv->mcast_addr_v6;
  }

  /* We support only one mcast group per family, make sure this transport
   * matches it. */
  if (!mcast_addr)
    goto no_addr;

  if (g_ascii_strcasecmp (tr->destination, mcast_addr->address) != 0 ||
      tr->port.min != mcast_addr->port ||
      tr->port.max != mcast_addr->port + mcast_addr->n_ports - 1 ||
      tr->ttl != mcast_addr->ttl)
    goto wrong_addr;

  return TRUE;

no_addr:
  {
    GST_WARNING_OBJECT (stream, "Adding mcast transport, but no mcast address "
        "has been reserved");
    return FALSE;
  }
wrong_addr:
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

  if (!alloc_ports (stream))
    goto no_ports;

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
    priv->send_rtp_sink = gst_element_get_request_pad (rtpbin, name);
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
    /* Need to connect our sinkpad from here */
    g_signal_connect (rtpbin, "pad-added", (GCallback) pad_added, stream);
    /* EOS */
    g_signal_connect (rtpbin, "on-npt-stop", (GCallback) on_npt_stop, stream);

    name = g_strdup_printf ("recv_rtp_sink_%u", idx);
    priv->recv_sink[0] = gst_element_get_request_pad (rtpbin, name);
    g_free (name);
  }

  name = g_strdup_printf ("send_rtcp_src_%u", idx);
  priv->send_src[1] = gst_element_get_request_pad (rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtcp_sink_%u", idx);
  priv->recv_sink[1] = gst_element_get_request_pad (rtpbin, name);
  g_free (name);

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

  create_sender_part (stream, bin, state);
  create_receiver_part (stream, bin, state);

  if (priv->srcpad) {
    /* be notified of caps changes */
    priv->caps_sig = g_signal_connect (priv->send_src[0], "notify::caps",
        (GCallback) caps_notify, stream);
    priv->caps = gst_pad_get_current_caps (priv->send_src[0]);
  }

  priv->joined_bin = bin;
  g_mutex_unlock (&priv->lock);

  return TRUE;

  /* ERRORS */
was_joined:
  {
    g_mutex_unlock (&priv->lock);
    return TRUE;
  }
no_ports:
  {
    g_mutex_unlock (&priv->lock);
    GST_WARNING ("failed to allocate ports %u", idx);
    return FALSE;
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

  g_mutex_lock (&priv->lock);
  if (priv->joined_bin == NULL)
    goto was_not_joined;
  if (priv->joined_bin != bin)
    goto wrong_bin;

  priv->joined_bin = NULL;

  /* all transports must be removed by now */
  if (priv->transports != NULL)
    goto transports_not_removed;

  clear_tr_cache (priv, TRUE);
  clear_tr_cache (priv, FALSE);

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

  for (i = 0; i < 2; i++) {
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

  gst_element_release_request_pad (rtpbin, priv->send_src[1]);
  gst_object_unref (priv->send_src[1]);
  priv->send_src[1] = NULL;

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
 * Return: (transfer full): the joined bin or NULL.
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
 * @rtptime: (allow-none): result RTP timestamp
 * @seq: (allow-none): result RTP seqnum
 * @clock_rate: (allow-none): the clock rate
 * @running_time: result running-time
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
  if (priv->udpsink[0] || priv->appsink[0]) {
    GstSample *last_sample;

    if (priv->udpsink[0])
      g_object_get (priv->udpsink[0], "last-sample", &last_sample, NULL);
    else
      g_object_get (priv->appsink[0], "last-sample", &last_sample, NULL);

    if (last_sample) {
      GstCaps *caps;
      GstBuffer *buffer;
      GstSegment *segment;
      GstRTPBuffer rtp_buffer = GST_RTP_BUFFER_INIT;

      caps = gst_sample_get_caps (last_sample);
      buffer = gst_sample_get_buffer (last_sample);
      segment = gst_sample_get_segment (last_sample);

      if (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp_buffer)) {
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
          GstStructure *s = gst_caps_get_structure (caps, 0);

          gst_structure_get_int (s, "clock-rate", (gint *) clock_rate);

          if (*clock_rate == 0 && running_time)
            *running_time = GST_CLOCK_TIME_NONE;
        }
        gst_sample_unref (last_sample);

        goto done;
      } else {
        gst_sample_unref (last_sample);
      }
    }
  }

  if (g_object_class_find_property (payobjclass, "stats")) {
    g_object_get (priv->payloader, "stats", &stats, NULL);
    if (stats == NULL)
      goto no_stats;

    if (seq)
      gst_structure_get_uint (stats, "seqnum", seq);

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
 * gst_rtsp_stream_get_caps:
 * @stream: a #GstRTSPStream
 *
 * Retrieve the current caps of @stream.
 *
 * Returns: (transfer full): the #GstCaps of @stream. use gst_caps_unref()
 * after usage.
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
static gboolean
update_transport (GstRTSPStream * stream, GstRTSPStreamTransport * trans,
    gboolean add)
{
  GstRTSPStreamPrivate *priv = stream->priv;
  const GstRTSPTransport *tr;

  tr = gst_rtsp_stream_transport_get_transport (trans);

  switch (tr->lower_transport) {
    case GST_RTSP_LOWER_TRANS_UDP_MCAST:
    {
      if (add) {
        if (!check_mcast_part_for_transport (stream, tr))
          goto mcast_error;
        priv->transports = g_list_prepend (priv->transports, trans);
      } else {
        priv->transports = g_list_remove (priv->transports, trans);
      }
      break;
    }
    case GST_RTSP_LOWER_TRANS_UDP:
    {
      gchar *dest;
      gint min, max;
      guint ttl = 0;

      dest = tr->destination;
      if (tr->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
        min = tr->port.min;
        max = tr->port.max;
        ttl = tr->ttl;
      } else if (priv->client_side) {
        /* In client side mode the 'destination' is the RTSP server, so send
         * to those ports */
        min = tr->server_port.min;
        max = tr->server_port.max;
      } else {
        min = tr->client_port.min;
        max = tr->client_port.max;
      }

      if (add) {
        if (ttl > 0) {
          GST_INFO ("setting ttl-mc %d", ttl);
          g_object_set (G_OBJECT (priv->udpsink[0]), "ttl-mc", ttl, NULL);
          g_object_set (G_OBJECT (priv->udpsink[1]), "ttl-mc", ttl, NULL);
        }
        GST_INFO ("adding %s:%d-%d", dest, min, max);
        g_signal_emit_by_name (priv->udpsink[0], "add", dest, min, NULL);
        g_signal_emit_by_name (priv->udpsink[1], "add", dest, max, NULL);
        priv->transports = g_list_prepend (priv->transports, trans);
      } else {
        GST_INFO ("removing %s:%d-%d", dest, min, max);
        g_signal_emit_by_name (priv->udpsink[0], "remove", dest, min, NULL);
        g_signal_emit_by_name (priv->udpsink[1], "remove", dest, max, NULL);
        priv->transports = g_list_remove (priv->transports, trans);
      }
      priv->transports_cookie++;
      break;
    }
    case GST_RTSP_LOWER_TRANS_TCP:
      if (add) {
        GST_INFO ("adding TCP %s", tr->destination);
        priv->transports = g_list_prepend (priv->transports, trans);
      } else {
        GST_INFO ("removing TCP %s", tr->destination);
        priv->transports = g_list_remove (priv->transports, trans);
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


/**
 * gst_rtsp_stream_add_transport:
 * @stream: a #GstRTSPStream
 * @trans: (transfer none): a #GstRTSPStreamTransport
 *
 * Add the transport in @trans to @stream. The media of @stream will
 * then also be send to the values configured in @trans.
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
  GstRTSPStreamPrivate *priv = GST_RTSP_STREAM_GET_PRIVATE (stream);
  GSocket *socket;
  const gchar *name;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (family == G_SOCKET_FAMILY_IPV4 ||
      family == G_SOCKET_FAMILY_IPV6, NULL);
  g_return_val_if_fail (priv->udpsink[0], NULL);

  if (family == G_SOCKET_FAMILY_IPV6)
    name = "socket-v6";
  else
    name = "socket";

  g_object_get (priv->udpsink[0], name, &socket, NULL);

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
  GstRTSPStreamPrivate *priv = GST_RTSP_STREAM_GET_PRIVATE (stream);
  GSocket *socket;
  const gchar *name;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (family == G_SOCKET_FAMILY_IPV4 ||
      family == G_SOCKET_FAMILY_IPV6, NULL);
  g_return_val_if_fail (priv->udpsink[1], NULL);

  if (family == G_SOCKET_FAMILY_IPV6)
    name = "socket-v6";
  else
    name = "socket";

  g_object_get (priv->udpsink[1], name, &socket, NULL);

  return socket;
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
pad_blocking (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstRTSPStreamPrivate *priv;
  GstRTSPStream *stream;

  stream = user_data;
  priv = stream->priv;

  GST_DEBUG_OBJECT (pad, "now blocking");

  g_mutex_lock (&priv->lock);
  priv->blocking = TRUE;
  g_mutex_unlock (&priv->lock);

  gst_element_post_message (priv->payloader,
      gst_message_new_element (GST_OBJECT_CAST (priv->payloader),
          gst_structure_new_empty ("GstRTSPStreamBlocking")));

  return GST_PAD_PROBE_OK;
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
  int i;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  if (blocked) {
    priv->blocking = FALSE;
    for (i = 0; i < 2; i++) {
      if (priv->blocked_id[i] == 0) {
        priv->blocked_id[i] = gst_pad_add_probe (priv->send_src[i],
            GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
            GST_PAD_PROBE_TYPE_BUFFER_LIST, pad_blocking,
            g_object_ref (stream), g_object_unref);
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
  gboolean ret;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  /* depending on the transport type, it should query corresponding sink */
  if ((priv->protocols & GST_RTSP_LOWER_TRANS_UDP) ||
      (priv->protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST))
    sink = priv->udpsink[0];
  else
    sink = priv->appsink[0];

  if (sink)
    gst_object_ref (sink);
  g_mutex_unlock (&priv->lock);

  if (!sink)
    return FALSE;

  ret = gst_element_query_position (sink, GST_FORMAT_TIME, position);
  gst_object_unref (sink);

  return ret;
}

/**
 * gst_rtsp_stream_query_stop:
 * @stream: a #GstRTSPStream
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
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), FALSE);

  priv = stream->priv;

  g_mutex_lock (&priv->lock);
  /* depending on the transport type, it should query corresponding sink */
  if ((priv->protocols & GST_RTSP_LOWER_TRANS_UDP) ||
      (priv->protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST))
    sink = priv->udpsink[0];
  else
    sink = priv->appsink[0];

  if (sink)
    gst_object_ref (sink);
  g_mutex_unlock (&priv->lock);

  if (!sink)
    return FALSE;

  query = gst_query_new_segment (GST_FORMAT_TIME);
  if ((ret = gst_element_query (sink, query))) {
    GstFormat format;

    gst_query_parse_segment (query, NULL, &format, NULL, stop);
    if (format != GST_FORMAT_TIME)
      *stop = -1;
  }
  gst_query_unref (query);
  gst_object_unref (sink);

  return ret;

}
