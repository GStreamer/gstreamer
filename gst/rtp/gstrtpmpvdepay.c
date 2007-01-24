/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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
#include "gstrtpmpvdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpmpvdepay_debug);
#define GST_CAT_DEFAULT (rtpmpvdepay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_mpvdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts MPEG video from RTP packets (RFC 2250)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpMPVDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate gst_rtp_mpv_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, systemstream = (boolean) FALSE")
    );

static GstStaticPadTemplate gst_rtp_mpv_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPV\";"
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_MPV_STRING ", "
        "clock-rate = (int) 90000")
    );

GST_BOILERPLATE (GstRtpMPVDepay, gst_rtp_mpv_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_mpv_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mpv_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_mpv_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mpv_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_mpv_depay_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_rtp_mpv_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpv_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpv_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpvdepay_details);
}

static void
gst_rtp_mpv_depay_class_init (GstRtpMPVDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_rtp_mpv_depay_set_property;
  gobject_class->get_property = gst_rtp_mpv_depay_get_property;

  gstelement_class->change_state = gst_rtp_mpv_depay_change_state;

  gstbasertpdepayload_class->set_caps = gst_rtp_mpv_depay_setcaps;
  gstbasertpdepayload_class->process = gst_rtp_mpv_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpmpvdepay_debug, "rtpmpvdepay", 0,
      "MPEG Video RTP Depayloader");
}

static void
gst_rtp_mpv_depay_init (GstRtpMPVDepay * rtpmpvdepay,
    GstRtpMPVDepayClass * klass)
{
}

static gboolean
gst_rtp_mpv_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpMPVDepay *rtpmpvdepay;
  gint clock_rate = 90000;      /* default */

  rtpmpvdepay = GST_RTP_MPV_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  depayload->clock_rate = clock_rate;

  return TRUE;
}

static GstBuffer *
gst_rtp_mpv_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMPVDepay *rtpmpvdepay;
  GstBuffer *outbuf;

  rtpmpvdepay = GST_RTP_MPV_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    guint8 T;
    guint32 timestamp;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    if (payload_len <= 4)
      goto empty_packet;

    /* 3.4 MPEG Video-specific header
     *
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |    MBZ  |T|         TR        | |N|S|B|E|  P  | | BFC | | FFC |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *                                  AN              FBV     FFV
     */
    T = (payload[0] & 0x04);

    payload_len -= 4;
    payload += 4;

    if (T) {
      /* 
       * 3.4.1 MPEG-2 Video-specific header extension
       *
       *  0                   1                   2                   3
       *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |X|E|f_[0,0]|f_[0,1]|f_[1,0]|f_[1,1]| DC| PS|T|P|C|Q|V|A|R|H|G|D|
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       */
      if (payload_len <= 4)
        goto empty_packet;

      payload_len -= 4;
      payload += 4;
    }

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);
    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    GST_DEBUG_OBJECT (rtpmpvdepay,
        "gst_rtp_mpv_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    return outbuf;
  }

  return NULL;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtpmpvdepay, STREAM, DECODE,
        ("Packet did not validate."), (NULL));
    return NULL;
  }
#if 0
bad_payload:
  {
    GST_DEBUG_OBJECT (rtpmpvdepay, "Unexpected payload type %u", pt);
    return NULL;
  }
#endif
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpmpvdepay, STREAM, DECODE,
        ("Empty payload."), (NULL));
    return NULL;
  }
}

static void
gst_rtp_mpv_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMPVDepay *rtpmpvdepay;

  rtpmpvdepay = GST_RTP_MPV_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mpv_depay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpMPVDepay *rtpmpvdepay;

  rtpmpvdepay = GST_RTP_MPV_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mpv_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpMPVDepay *rtpmpvdepay;
  GstStateChangeReturn ret;

  rtpmpvdepay = GST_RTP_MPV_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
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
gst_rtp_mpv_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpvdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_MPV_DEPAY);
}
