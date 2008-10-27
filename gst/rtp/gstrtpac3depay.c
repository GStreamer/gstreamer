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
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpac3depay.h"

GST_DEBUG_CATEGORY_STATIC (rtpac3depay_debug);
#define GST_CAT_DEFAULT (rtpac3depay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_ac3depay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts AC3 audio from RTP packets (RFC 4184)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_ac3_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/ac3")
    );

static GstStaticPadTemplate gst_rtp_ac3_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) { 32000, 44100, 48000 }, "
        "encoding-name = (string) \"AC3\"")
    );

GST_BOILERPLATE (GstRtpAC3Depay, gst_rtp_ac3_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_ac3_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_ac3_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static GstStateChangeReturn gst_rtp_ac3_depay_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_rtp_ac3_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_ac3_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_ac3_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_ac3depay_details);
}

static void
gst_rtp_ac3_depay_class_init (GstRtpAC3DepayClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_rtp_ac3_depay_change_state;

  gstbasertpdepayload_class->set_caps = gst_rtp_ac3_depay_setcaps;
  gstbasertpdepayload_class->process = gst_rtp_ac3_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpac3depay_debug, "rtpac3depay", 0,
      "MPEG Audio RTP Depayloader");
}

static void
gst_rtp_ac3_depay_init (GstRtpAC3Depay * rtpac3depay,
    GstRtpAC3DepayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static gboolean
gst_rtp_ac3_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpAC3Depay *rtpac3depay;
  gint clock_rate;
  GstCaps *srccaps;
  gboolean res;

  rtpac3depay = GST_RTP_AC3_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("audio/ac3", NULL);
  res = gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return res;
}

struct frmsize_s
{
  guint16 bit_rate;
  guint16 frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[] = {
  {32, {64, 69, 96}},
  {32, {64, 70, 96}},
  {40, {80, 87, 120}},
  {40, {80, 88, 120}},
  {48, {96, 104, 144}},
  {48, {96, 105, 144}},
  {56, {112, 121, 168}},
  {56, {112, 122, 168}},
  {64, {128, 139, 192}},
  {64, {128, 140, 192}},
  {80, {160, 174, 240}},
  {80, {160, 175, 240}},
  {96, {192, 208, 288}},
  {96, {192, 209, 288}},
  {112, {224, 243, 336}},
  {112, {224, 244, 336}},
  {128, {256, 278, 384}},
  {128, {256, 279, 384}},
  {160, {320, 348, 480}},
  {160, {320, 349, 480}},
  {192, {384, 417, 576}},
  {192, {384, 418, 576}},
  {224, {448, 487, 672}},
  {224, {448, 488, 672}},
  {256, {512, 557, 768}},
  {256, {512, 558, 768}},
  {320, {640, 696, 960}},
  {320, {640, 697, 960}},
  {384, {768, 835, 1152}},
  {384, {768, 836, 1152}},
  {448, {896, 975, 1344}},
  {448, {896, 976, 1344}},
  {512, {1024, 1114, 1536}},
  {512, {1024, 1115, 1536}},
  {576, {1152, 1253, 1728}},
  {576, {1152, 1254, 1728}},
  {640, {1280, 1393, 1920}},
  {640, {1280, 1394, 1920}}
};

static GstBuffer *
gst_rtp_ac3_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpAC3Depay *rtpac3depay;
  GstBuffer *outbuf;

  rtpac3depay = GST_RTP_AC3_DEPAY (depayload);

  {
    gint payload_len;
    guint8 *payload;
    guint16 FT, NF;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    if (payload_len <= 2)
      goto empty_packet;

    /* strip off header
     *
     *  0                   1
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |    MBZ    | FT|       NF      |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    FT = payload[0] & 0x3;
    NF = payload[1];

    GST_DEBUG_OBJECT (rtpac3depay, "FT: %d, NF: %d", FT, NF);

    payload_len -= 2;
    payload += 2;

    /* We don't bother with fragmented packets yet */
    outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 2, -1);

    GST_DEBUG_OBJECT (rtpac3depay, "pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    return outbuf;
  }

  return NULL;

  /* ERRORS */
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpac3depay, STREAM, DECODE,
        ("Empty Payload."), (NULL));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_ac3_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpAC3Depay *rtpac3depay;
  GstStateChangeReturn ret;

  rtpac3depay = GST_RTP_AC3_DEPAY (element);

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
gst_rtp_ac3_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpac3depay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_AC3_DEPAY);
}
