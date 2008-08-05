/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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
#include "gstrtpmp1sdepay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_mp1sdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts MPEG1 System Streams from RTP packets (RFC 3555)",
    "Wim Taymans <wim.taymans@gmail.com>");

/* RtpMP1SDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LAST
};

static GstStaticPadTemplate gst_rtp_mp1s_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg,systemstream=(boolean)true")
    );

/* The spec says video/MP1S but I have seen streams with other/MP1S so we will
 * allow them both */
static GstStaticPadTemplate gst_rtp_mp1s_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"other\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP1S\";"
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP1S\"")
    );

GST_BOILERPLATE (GstRtpMP1SDepay, gst_rtp_mp1s_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_mp1s_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mp1s_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_mp1s_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mp1s_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_mp1s_depay_change_state (GstElement *
    element, GstStateChange transition);


static void
gst_rtp_mp1s_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp1s_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp1s_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mp1sdepay_details);
}

static void
gst_rtp_mp1s_depay_class_init (GstRtpMP1SDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_mp1s_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_mp1s_depay_setcaps;

  gobject_class->set_property = gst_rtp_mp1s_depay_set_property;
  gobject_class->get_property = gst_rtp_mp1s_depay_get_property;

  gstelement_class->change_state = gst_rtp_mp1s_depay_change_state;
}

static void
gst_rtp_mp1s_depay_init (GstRtpMP1SDepay * rtpmp1sdepay,
    GstRtpMP1SDepayClass * klass)
{
}

static gboolean
gst_rtp_mp1s_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure;
  GstRtpMP1SDepay *rtpmp1sdepay;
  gint clock_rate = 90000;      /* default */

  rtpmp1sdepay = GST_RTP_MP1S_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

static GstBuffer *
gst_rtp_mp1s_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMP1SDepay *rtpmp1sdepay;
  GstBuffer *outbuf;
  gint payload_len;

  rtpmp1sdepay = GST_RTP_MP1S_DEPAY (depayload);

  if (G_UNLIKELY (!gst_rtp_buffer_validate (buf)))
    goto bad_packet;

  payload_len = gst_rtp_buffer_get_payload_len (buf);

  outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 0, -1);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

  GST_DEBUG ("gst_rtp_mp1s_depay_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  return outbuf;

  /* ERRORS */
bad_packet:
  {
    GST_ELEMENT_WARNING (rtpmp1sdepay, STREAM, DECODE,
        (NULL), ("Packet did not validate"));
    return NULL;
  }
}

static void
gst_rtp_mp1s_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP1SDepay *rtpmp1sdepay;

  rtpmp1sdepay = GST_RTP_MP1S_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mp1s_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP1SDepay *rtpmp1sdepay;

  rtpmp1sdepay = GST_RTP_MP1S_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mp1s_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpMP1SDepay *rtpmp1sdepay;
  GstStateChangeReturn ret;

  rtpmp1sdepay = GST_RTP_MP1S_DEPAY (element);

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
gst_rtp_mp1s_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp1sdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_MP1S_DEPAY);
}
