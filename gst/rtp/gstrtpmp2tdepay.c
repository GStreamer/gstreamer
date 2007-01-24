/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpmp2tdepay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_mp2tdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts MPEG2 TS from RTP packets (RFC 2250)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpMP2TDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FREQUENCY
};

static GstStaticPadTemplate gst_rtp_mp2t_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts,"
        "packetsize=(int)188," "systemstream=(boolean)true")
    );

static GstStaticPadTemplate gst_rtp_mp2t_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP2T-ES\";"
        /* All optional parameters
         *
         * "profile-level-id=[1,MAX]"
         * "config=" 
         */
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_MP2T_STRING ", "
        "clock-rate = (int) [1, MAX ]")
    );

GST_BOILERPLATE (GstRtpMP2TDepay, gst_rtp_mp2t_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_mp2t_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mp2t_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_mp2t_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mp2t_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_mp2t_depay_change_state (GstElement *
    element, GstStateChange transition);


static void
gst_rtp_mp2t_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp2t_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp2t_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mp2tdepay_details);
}

static void
gst_rtp_mp2t_depay_class_init (GstRtpMP2TDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_mp2t_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_mp2t_depay_setcaps;

  gobject_class->set_property = gst_rtp_mp2t_depay_set_property;
  gobject_class->get_property = gst_rtp_mp2t_depay_get_property;

  gstelement_class->change_state = gst_rtp_mp2t_depay_change_state;
}

static void
gst_rtp_mp2t_depay_init (GstRtpMP2TDepay * rtpmp2tdepay,
    GstRtpMP2TDepayClass * klass)
{
}

static gboolean
gst_rtp_mp2t_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{

  GstStructure *structure;
  GstRtpMP2TDepay *rtpmp2tdepay;
  GstCaps *srccaps;
  gint clock_rate = 90000;      /* default */

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_field (structure, "clock-rate")) {
    gst_structure_get_int (structure, "clock-rate", &clock_rate);
  }

  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("video/mpegts",
      "systemstream", G_TYPE_BOOLEAN, TRUE,
      "packetsize", G_TYPE_INT, 188, NULL);

  return TRUE;
}

static GstBuffer *
gst_rtp_mp2t_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMP2TDepay *rtpmp2tdepay;
  GstBuffer *outbuf;
  gint payload_len;
  guint8 *payload;
  guint32 timestamp;

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  payload_len = gst_rtp_buffer_get_payload_len (buf);
  payload = gst_rtp_buffer_get_payload (buf);

  timestamp = gst_rtp_buffer_get_timestamp (buf);

  outbuf = gst_buffer_new_and_alloc (payload_len);
  memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));
  GST_BUFFER_TIMESTAMP (outbuf) =
      gst_util_uint64_scale_int (timestamp, GST_SECOND, depayload->clock_rate);

  GST_DEBUG ("gst_rtp_mp2t_depay_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  return outbuf;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtpmp2tdepay, STREAM, DECODE,
        ("Packet did not validate"), (NULL));
    return NULL;
  }
}

static void
gst_rtp_mp2t_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP2TDepay *rtpmp2tdepay;

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mp2t_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP2TDepay *rtpmp2tdepay;

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mp2t_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpMP2TDepay *rtpmp2tdepay;
  GstStateChangeReturn ret;

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_mp2t_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp2tdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_MP2T_DEPAY);
}
