/* GStreamer
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "transportstream.h"
#include "transportsendbin.h"
#include "transportreceivebin.h"
#include "gstwebrtcice.h"
#include "gstwebrtcbin.h"
#include "utils.h"

#define transport_stream_parent_class parent_class
G_DEFINE_TYPE (TransportStream, transport_stream, GST_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_WEBRTC,
  PROP_SESSION_ID,
  PROP_RTCP_MUX,
  PROP_DTLS_CLIENT,
};

static void
transport_stream_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  TransportStream *stream = TRANSPORT_STREAM (object);

  switch (prop_id) {
    case PROP_WEBRTC:
      gst_object_set_parent (GST_OBJECT (stream), g_value_get_object (value));
      break;
  }

  GST_OBJECT_LOCK (stream);
  switch (prop_id) {
    case PROP_WEBRTC:
      break;
    case PROP_SESSION_ID:
      stream->session_id = g_value_get_uint (value);
      break;
    case PROP_RTCP_MUX:
      stream->rtcp_mux = g_value_get_boolean (value);
      break;
    case PROP_DTLS_CLIENT:
      stream->dtls_client = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (stream);
}

static void
transport_stream_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  TransportStream *stream = TRANSPORT_STREAM (object);

  GST_OBJECT_LOCK (stream);
  switch (prop_id) {
    case PROP_SESSION_ID:
      g_value_set_uint (value, stream->session_id);
      break;
    case PROP_RTCP_MUX:
      g_value_set_boolean (value, stream->rtcp_mux);
      break;
    case PROP_DTLS_CLIENT:
      g_value_set_boolean (value, stream->dtls_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (stream);
}

static void
transport_stream_dispose (GObject * object)
{
  TransportStream *stream = TRANSPORT_STREAM (object);

  if (stream->send_bin)
    gst_object_unref (stream->send_bin);
  stream->send_bin = NULL;

  if (stream->receive_bin)
    gst_object_unref (stream->receive_bin);
  stream->receive_bin = NULL;

  if (stream->transport)
    gst_object_unref (stream->transport);
  stream->transport = NULL;

  if (stream->rtcp_transport)
    gst_object_unref (stream->rtcp_transport);
  stream->rtcp_transport = NULL;

  GST_OBJECT_PARENT (object) = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
transport_stream_finalize (GObject * object)
{
  TransportStream *stream = TRANSPORT_STREAM (object);

  g_array_free (stream->ptmap, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
transport_stream_constructed (GObject * object)
{
  TransportStream *stream = TRANSPORT_STREAM (object);
  GstWebRTCBin *webrtc;
  GstWebRTCICETransport *ice_trans;

  stream->transport = gst_webrtc_dtls_transport_new (stream->session_id, FALSE);
  stream->rtcp_transport =
      gst_webrtc_dtls_transport_new (stream->session_id, TRUE);

  webrtc = GST_WEBRTC_BIN (gst_object_get_parent (GST_OBJECT (object)));

  g_object_bind_property (stream->transport, "client", stream, "dtls-client",
      G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (stream->rtcp_transport, "client", stream,
      "dtls-client", G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (stream->transport, "certificate",
      stream->rtcp_transport, "certificate", G_BINDING_BIDIRECTIONAL);

  /* Need to go full Java and have a transport manager?
   * Or make the caller set the ICE transport up? */

  stream->stream = _find_ice_stream_for_session (webrtc, stream->session_id);
  if (stream->stream == NULL) {
    stream->stream = gst_webrtc_ice_add_stream (webrtc->priv->ice,
        stream->session_id);
    _add_ice_stream_item (webrtc, stream->session_id, stream->stream);
  }
  ice_trans =
      gst_webrtc_ice_find_transport (webrtc->priv->ice, stream->stream,
      GST_WEBRTC_ICE_COMPONENT_RTP);
  gst_webrtc_dtls_transport_set_transport (stream->transport, ice_trans);
  gst_object_unref (ice_trans);

  ice_trans =
      gst_webrtc_ice_find_transport (webrtc->priv->ice, stream->stream,
      GST_WEBRTC_ICE_COMPONENT_RTCP);
  gst_webrtc_dtls_transport_set_transport (stream->rtcp_transport, ice_trans);
  gst_object_unref (ice_trans);

  stream->send_bin = g_object_new (transport_send_bin_get_type (), "stream",
      stream, NULL);
  gst_object_ref_sink (stream->send_bin);
  stream->receive_bin = g_object_new (transport_receive_bin_get_type (),
      "stream", stream, NULL);
  gst_object_ref_sink (stream->receive_bin);

  gst_object_unref (webrtc);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
transport_stream_class_init (TransportStreamClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->constructed = transport_stream_constructed;
  gobject_class->get_property = transport_stream_get_property;
  gobject_class->set_property = transport_stream_set_property;
  gobject_class->dispose = transport_stream_dispose;
  gobject_class->finalize = transport_stream_finalize;

  /* some acrobatics are required to set the parent before _constructed()
   * has been called */
  g_object_class_install_property (gobject_class,
      PROP_WEBRTC,
      g_param_spec_object ("webrtc", "Parent webrtcbin",
          "Parent webrtcbin",
          GST_TYPE_WEBRTC_BIN,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SESSION_ID,
      g_param_spec_uint ("session-id", "Session ID",
          "Session ID used for this transport",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RTCP_MUX,
      g_param_spec_boolean ("rtcp-mux", "RTCP Mux",
          "Whether RTCP packets are muxed with RTP packets",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DTLS_CLIENT,
      g_param_spec_boolean ("dtls-client", "DTLS client",
          "Whether we take the client role in DTLS negotiation",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
clear_ptmap_item (PtMapItem * item)
{
  if (item->caps)
    gst_caps_unref (item->caps);
}

static void
transport_stream_init (TransportStream * stream)
{
  stream->ptmap = g_array_new (FALSE, TRUE, sizeof (PtMapItem));
  g_array_set_clear_func (stream->ptmap, (GDestroyNotify) clear_ptmap_item);
}

TransportStream *
transport_stream_new (GstWebRTCBin * webrtc, guint session_id)
{
  TransportStream *stream;

  stream = g_object_new (transport_stream_get_type (), "webrtc", webrtc,
      "session-id", session_id, NULL);

  return stream;
}
