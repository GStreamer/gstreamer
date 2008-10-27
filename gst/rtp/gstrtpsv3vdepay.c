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

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpsv3vdepay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_sv3vdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts SVQ3 video from RTP packets (no RFC)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_sv3v_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-svq, " "svqversion = (int) 3")
    );

static GstStaticPadTemplate gst_rtp_sv3v_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, "
        "encoding-name = (string) { \"X-SV3V-ES\", \"X-SORENSON-VIDEO\" }")
    );

GST_BOILERPLATE (GstRtpSV3VDepay, gst_rtp_sv3v_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_sv3v_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_sv3v_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstBuffer *gst_rtp_sv3v_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
gboolean gst_rtp_sv3v_depay_setcaps (GstBaseRTPDepayload * filter,
    GstCaps * caps);

static void
gst_rtp_sv3v_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_sv3v_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_sv3v_depay_sink_template));


  gst_element_class_set_details (element_class, &gst_rtp_sv3vdepay_details);
}

static void
gst_rtp_sv3v_depay_class_init (GstRtpSV3VDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_sv3v_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_sv3v_depay_setcaps;

  gobject_class->finalize = gst_rtp_sv3v_depay_finalize;

  gstelement_class->change_state = gst_rtp_sv3v_depay_change_state;
}

static void
gst_rtp_sv3v_depay_init (GstRtpSV3VDepay * rtpsv3vdepay,
    GstRtpSV3VDepayClass * klass)
{
  rtpsv3vdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_sv3v_depay_finalize (GObject * object)
{
  GstRtpSV3VDepay *rtpsv3vdepay;

  rtpsv3vdepay = GST_RTP_SV3V_DEPAY (object);

  g_object_unref (rtpsv3vdepay->adapter);
  rtpsv3vdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

// only on the sink
gboolean
gst_rtp_sv3v_depay_setcaps (GstBaseRTPDepayload * filter, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint clock_rate;

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         // default
  filter->clock_rate = clock_rate;

  /* will set caps later */

  return TRUE;
}

static GstBuffer *
gst_rtp_sv3v_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpSV3VDepay *rtpsv3vdepay;
  GstBuffer *outbuf;
  guint16 seq;

  rtpsv3vdepay = GST_RTP_SV3V_DEPAY (depayload);

  /* flush on sequence number gaps */
  seq = gst_rtp_buffer_get_seq (buf);
  if (seq != rtpsv3vdepay->nextseq) {
    gst_adapter_clear (rtpsv3vdepay->adapter);
  }
  rtpsv3vdepay->nextseq = seq + 1;

  {
    gint payload_len;
    guint8 *payload;
    gboolean M;
    gboolean C, S, E;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    if (payload_len < 3)
      goto bad_packet;

    payload = gst_rtp_buffer_get_payload (buf);

    M = gst_rtp_buffer_get_marker (buf);

    /* This is all a guess:
     *                      1 1 1 1 1 1
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |0|C|S|E|0|0|0|0|0|0|0|0|0|0|0|0|
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *
     * C: config, packet contains config info
     * S: start, packet contains start of frame
     * E: end, packet contains end of frame
     */
    /* this seems to indicate a packet with a config string sent before each
     * keyframe */
    C = (payload[0] & 0x40) == 0x40;

    /* redundant with the RTP marker bit */
    S = (payload[0] & 0x20) == 0x20;
    E = (payload[0] & 0x10) == 0x10;

    if (C) {
      GstCaps *caps;
      GstBuffer *codec_data;
      GValue value = { 0 };

      /* if we already have caps, we don't need to do anything. FIXME, check if
       * something changed. */
      if (GST_PAD_CAPS (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload)))
        return NULL;

      /* No idea... These are the two examples I found.. */
      if (payload[2] == 0x1d) {
        rtpsv3vdepay->width = 160;
        rtpsv3vdepay->height = 128;
      } else if (payload[2] == 0xdd) {
        rtpsv3vdepay->width = 320;
        rtpsv3vdepay->height = 240;
      }

      /* we need a dummy empty codec data */
      g_value_init (&value, GST_TYPE_BUFFER);
      gst_value_deserialize (&value, "");
      codec_data = gst_value_get_buffer (&value);

      caps = gst_caps_new_simple ("video/x-svq",
          "svqversion", G_TYPE_INT, 3,
          "width", G_TYPE_INT, rtpsv3vdepay->width,
          "height", G_TYPE_INT, rtpsv3vdepay->height,
          "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
      gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), caps);
      gst_caps_unref (caps);
      g_value_unset (&value);

      return NULL;
    }

    /* store data in adapter, stip off 2 bytes header */
    outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 2, -1);
    gst_adapter_push (rtpsv3vdepay->adapter, outbuf);

    if (M) {
      /* frame is completed: push contents of adapter */
      guint avail;

      avail = gst_adapter_available (rtpsv3vdepay->adapter);
      outbuf = gst_adapter_take_buffer (rtpsv3vdepay->adapter, avail);

      return outbuf;
    }
  }
  return NULL;

  /* ERRORS */
bad_packet:
  {
    GST_ELEMENT_WARNING (rtpsv3vdepay, STREAM, DECODE,
        (NULL), ("Packet was too short"));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_sv3v_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpSV3VDepay *rtpsv3vdepay;
  GstStateChangeReturn ret;

  rtpsv3vdepay = GST_RTP_SV3V_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpsv3vdepay->adapter);
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
gst_rtp_sv3v_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpsv3vdepay",
      GST_RANK_NONE, GST_TYPE_RTP_SV3V_DEPAY);
}
