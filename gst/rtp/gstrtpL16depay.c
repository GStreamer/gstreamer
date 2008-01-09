/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include "gstrtpL16depay.h"

GST_DEBUG_CATEGORY_STATIC (rtpL16depay_debug);
#define GST_CAT_DEFAULT (rtpL16depay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_L16_depay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts raw audio from RTP packets",
    "Zeeshan Ali <zak147@yahoo.com>," "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_L16_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BIG_ENDIAN, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate gst_rtp_L16_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [ 1, MAX ], "
        /* "channels = (int) [1, MAX]"  */
        /* "emphasis = (string) ANY" */
        /* "channel-order = (string) ANY" */
        "encoding-name = (string) \"L16\";"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) { " GST_RTP_PAYLOAD_L16_STEREO_STRING ", "
        GST_RTP_PAYLOAD_L16_MONO_STRING " }," "clock-rate = (int) [ 1, MAX ]"
        /* "channels = (int) [1, MAX]" */
        /* "emphasis = (string) ANY" */
        /* "channel-order = (string) ANY" */
    )
    );

GST_BOILERPLATE (GstRtpL16Depay, gst_rtp_L16_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_L16_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_L16_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static GstStateChangeReturn gst_rtp_L16_depay_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_rtp_L16_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_L16_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_L16_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_L16_depay_details);
}

static void
gst_rtp_L16_depay_class_init (GstRtpL16DepayClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_rtp_L16_depay_change_state;

  gstbasertpdepayload_class->set_caps = gst_rtp_L16_depay_setcaps;
  gstbasertpdepayload_class->process = gst_rtp_L16_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpL16depay_debug, "rtpL16depay", 0,
      "Raw Audio RTP Depayloader");
}

static void
gst_rtp_L16_depay_init (GstRtpL16Depay * rtpL16depay,
    GstRtpL16DepayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static gint
gst_rtp_L16_depay_parse_int (GstStructure * structure, const gchar * field,
    gint def)
{
  const gchar *str;
  gint res;

  if ((str = gst_structure_get_string (structure, field)))
    return atoi (str);

  if (gst_structure_get_int (structure, field, &res))
    return res;

  return def;
}

static gboolean
gst_rtp_L16_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpL16Depay *rtpL16depay;
  gint clock_rate, payload;
  gint channels;
  GstCaps *srccaps;

  rtpL16depay = GST_RTP_L16_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  payload = 96;
  gst_structure_get_int (structure, "payload", &payload);
  switch (payload) {
    case GST_RTP_PAYLOAD_L16_STEREO:
      channels = 2;
      clock_rate = 44100;
      break;
    case GST_RTP_PAYLOAD_L16_MONO:
      channels = 1;
      clock_rate = 44100;
      break;
    default:
      channels = 0;
      clock_rate = 0;
      break;
  }

  /* caps can overwrite defaults */
  clock_rate =
      gst_rtp_L16_depay_parse_int (structure, "clock-rate", clock_rate);
  channels = gst_rtp_L16_depay_parse_int (structure, "channels", channels);

  depayload->clock_rate = clock_rate;
  rtpL16depay->rate = clock_rate;
  rtpL16depay->channels = channels;

  srccaps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", G_TYPE_INT, clock_rate, "channels", G_TYPE_INT, channels, NULL);

  gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

static GstBuffer *
gst_rtp_L16_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpL16Depay *rtpL16depay;
  GstBuffer *outbuf;

  rtpL16depay = GST_RTP_L16_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;

    payload_len = gst_rtp_buffer_get_payload_len (buf);

    if (payload_len <= 0)
      goto empty_packet;

    GST_DEBUG_OBJECT (rtpL16depay, "got payload of %d bytes", payload_len);

    outbuf = gst_rtp_buffer_get_payload_buffer (buf);

    return outbuf;
  }
  return NULL;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtpL16depay, STREAM, DECODE,
        ("Packet did not validate."), (NULL));
    return NULL;
  }
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpL16depay, STREAM, DECODE,
        ("Empty Payload."), (NULL));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_L16_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpL16Depay *rtpL16depay;
  GstStateChangeReturn ret;

  rtpL16depay = GST_RTP_L16_DEPAY (element);

  /*
     switch (transition) {
     case GST_STATE_CHANGE_NULL_TO_READY:
     break;
     default:
     break;
     }
   */

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /*
     switch (transition) {
     case GST_STATE_CHANGE_READY_TO_NULL:
     break;
     default:
     break;
     }
   */
  return ret;
}

gboolean
gst_rtp_L16_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16depay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_L16_DEPAY);
}
