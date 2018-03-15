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
 * SECTION:gstwebrtc-sender
 * @short_description: RTCRtpSender object
 * @title: GstWebRTCRTPSender
 * @see_also: #GstWebRTCRTPReceiver, #GstWebRTCRTPTransceiver
 *
 * <ulink url="https://www.w3.org/TR/webrtc/#rtcrtpsender-interface">https://www.w3.org/TR/webrtc/#rtcrtpsender-interface</ulink>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "rtpsender.h"
#include "rtptransceiver.h"

#define GST_CAT_DEFAULT gst_webrtc_rtp_sender_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_webrtc_rtp_sender_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCRTPSender, gst_webrtc_rtp_sender,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (gst_webrtc_rtp_sender_debug,
        "webrtcsender", 0, "webrtcsender");
    );

enum
{
  SIGNAL_0,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_MID,
  PROP_SENDER,
  PROP_STOPPED,
  PROP_DIRECTION,
};

//static guint gst_webrtc_rtp_sender_signals[LAST_SIGNAL] = { 0 };

void
gst_webrtc_rtp_sender_set_transport (GstWebRTCRTPSender * sender,
    GstWebRTCDTLSTransport * transport)
{
  g_return_if_fail (GST_IS_WEBRTC_RTP_SENDER (sender));
  g_return_if_fail (GST_IS_WEBRTC_DTLS_TRANSPORT (transport));

  GST_OBJECT_LOCK (sender);
  gst_object_replace ((GstObject **) & sender->transport,
      GST_OBJECT (transport));
  GST_OBJECT_UNLOCK (sender);
}

void
gst_webrtc_rtp_sender_set_rtcp_transport (GstWebRTCRTPSender * sender,
    GstWebRTCDTLSTransport * transport)
{
  g_return_if_fail (GST_IS_WEBRTC_RTP_SENDER (sender));
  g_return_if_fail (GST_IS_WEBRTC_DTLS_TRANSPORT (transport));

  GST_OBJECT_LOCK (sender);
  gst_object_replace ((GstObject **) & sender->rtcp_transport,
      GST_OBJECT (transport));
  GST_OBJECT_UNLOCK (sender);
}

static void
gst_webrtc_rtp_sender_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_sender_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_sender_finalize (GObject * object)
{
  GstWebRTCRTPSender *webrtc = GST_WEBRTC_RTP_SENDER (object);

  if (webrtc->transport)
    gst_object_unref (webrtc->transport);
  webrtc->transport = NULL;

  if (webrtc->rtcp_transport)
    gst_object_unref (webrtc->rtcp_transport);
  webrtc->rtcp_transport = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_rtp_sender_class_init (GstWebRTCRTPSenderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_webrtc_rtp_sender_get_property;
  gobject_class->set_property = gst_webrtc_rtp_sender_set_property;
  gobject_class->finalize = gst_webrtc_rtp_sender_finalize;
}

static void
gst_webrtc_rtp_sender_init (GstWebRTCRTPSender * webrtc)
{
}

GstWebRTCRTPSender *
gst_webrtc_rtp_sender_new (void)
{
  return g_object_new (GST_TYPE_WEBRTC_RTP_SENDER, NULL);
}
