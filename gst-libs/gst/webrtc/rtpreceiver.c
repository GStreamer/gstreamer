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

/**
 * SECTION:gstwebrtc-receiver
 * @short_description: RTCRtpReceiver object
 * @title: GstWebRTCRTPReceiver
 * @see_also: #GstWebRTCRTPSender, #GstWebRTCRTPTransceiver
 *
 * <ulink url="https://www.w3.org/TR/webrtc/#rtcrtpreceiver-interface">https://www.w3.org/TR/webrtc/#rtcrtpreceiver-interface</ulink>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "rtpreceiver.h"

#define GST_CAT_DEFAULT gst_webrtc_rtp_receiver_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_webrtc_rtp_receiver_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCRTPReceiver, gst_webrtc_rtp_receiver,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (gst_webrtc_rtp_receiver_debug,
        "webrtcreceiver", 0, "webrtcreceiver"););

enum
{
  SIGNAL_0,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
};

//static guint gst_webrtc_rtp_receiver_signals[LAST_SIGNAL] = { 0 };

void
gst_webrtc_rtp_receiver_set_transport (GstWebRTCRTPReceiver * receiver,
    GstWebRTCDTLSTransport * transport)
{
  g_return_if_fail (GST_IS_WEBRTC_RTP_RECEIVER (receiver));
  g_return_if_fail (GST_IS_WEBRTC_DTLS_TRANSPORT (transport));

  GST_OBJECT_LOCK (receiver);
  gst_object_replace ((GstObject **) & receiver->transport,
      GST_OBJECT (transport));
  GST_OBJECT_UNLOCK (receiver);
}

void
gst_webrtc_rtp_receiver_set_rtcp_transport (GstWebRTCRTPReceiver * receiver,
    GstWebRTCDTLSTransport * transport)
{
  g_return_if_fail (GST_IS_WEBRTC_RTP_RECEIVER (receiver));
  g_return_if_fail (GST_IS_WEBRTC_DTLS_TRANSPORT (transport));

  GST_OBJECT_LOCK (receiver);
  gst_object_replace ((GstObject **) & receiver->rtcp_transport,
      GST_OBJECT (transport));
  GST_OBJECT_UNLOCK (receiver);
}

static void
gst_webrtc_rtp_receiver_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_receiver_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_receiver_finalize (GObject * object)
{
  GstWebRTCRTPReceiver *webrtc = GST_WEBRTC_RTP_RECEIVER (object);

  if (webrtc->transport)
    gst_object_unref (webrtc->transport);
  webrtc->transport = NULL;

  if (webrtc->rtcp_transport)
    gst_object_unref (webrtc->rtcp_transport);
  webrtc->rtcp_transport = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_rtp_receiver_class_init (GstWebRTCRTPReceiverClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_webrtc_rtp_receiver_get_property;
  gobject_class->set_property = gst_webrtc_rtp_receiver_set_property;
  gobject_class->finalize = gst_webrtc_rtp_receiver_finalize;
}

static void
gst_webrtc_rtp_receiver_init (GstWebRTCRTPReceiver * webrtc)
{
}

GstWebRTCRTPReceiver *
gst_webrtc_rtp_receiver_new (void)
{
  return g_object_new (GST_TYPE_WEBRTC_RTP_RECEIVER, NULL);
}
