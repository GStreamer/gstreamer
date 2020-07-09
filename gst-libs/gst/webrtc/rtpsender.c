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
 * <https://www.w3.org/TR/webrtc/#rtcrtpsender-interface>
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
  PROP_PRIORITY
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

/**
 * gst_webrtc_rtp_sender_set_priority:
 * @sender: a #GstWebRTCRTPSender
 * @priority: The priority of this sender
 *
 * Sets the content of the IPv4 Type of Service (ToS), also known as DSCP
 * (Differentiated Services Code Point).
 * This also sets the Traffic Class field of IPv6.
 *
 * Since: 1.20
 */

void
gst_webrtc_rtp_sender_set_priority (GstWebRTCRTPSender * sender,
    GstWebRTCPriorityType priority)
{
  GST_OBJECT_LOCK (sender);
  sender->priority = priority;
  GST_OBJECT_UNLOCK (sender);
  g_object_notify (G_OBJECT (sender), "priority");
}

static void
gst_webrtc_rtp_sender_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCRTPSender *sender = GST_WEBRTC_RTP_SENDER (object);

  switch (prop_id) {
    case PROP_PRIORITY:
      gst_webrtc_rtp_sender_set_priority (sender, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_sender_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCRTPSender *sender = GST_WEBRTC_RTP_SENDER (object);

  switch (prop_id) {
    case PROP_PRIORITY:
      GST_OBJECT_LOCK (sender);
      g_value_set_uint (value, sender->priority);
      GST_OBJECT_UNLOCK (sender);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_sender_finalize (GObject * object)
{
  GstWebRTCRTPSender *sender = GST_WEBRTC_RTP_SENDER (object);

  if (sender->transport)
    gst_object_unref (sender->transport);
  sender->transport = NULL;

  if (sender->rtcp_transport)
    gst_object_unref (sender->rtcp_transport);
  sender->rtcp_transport = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_rtp_sender_class_init (GstWebRTCRTPSenderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_webrtc_rtp_sender_get_property;
  gobject_class->set_property = gst_webrtc_rtp_sender_set_property;
  gobject_class->finalize = gst_webrtc_rtp_sender_finalize;

  /**
   * GstWebRTCRTPSender:priority:
   *
   * The priority from which to set the DSCP field on packets
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class,
      PROP_PRIORITY,
      g_param_spec_enum ("priority",
          "Priority",
          "The priority from which to set the DSCP field on packets",
          GST_TYPE_WEBRTC_PRIORITY_TYPE, GST_WEBRTC_PRIORITY_TYPE_LOW,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
