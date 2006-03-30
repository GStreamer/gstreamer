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
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpamrdepay.h"

/* references:
 *
 * RFC 3267 - Real-Time Transport Protocol (RTP) Payload Format and File Storage Format 
 *   for the Adaptive Multi-Rate (AMR) and Adaptive Multi-Rate Wideband (AMR-WB) Audio 
 *   Codecs.
 */

/* elementfactory information */
static GstElementDetails gst_rtp_amrdepay_details =
GST_ELEMENT_DETAILS ("RTP packet parser",
    "Codec/Depayr/Network",
    "Extracts AMR audio from RTP packets (RFC 3267)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpAMRDepay signals and args */
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

/* input is an RTP packet 
 *
 * params see RFC 3267, section 8.1
 */
static GstStaticPadTemplate gst_rtp_amr_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"AMR\", "
        "encoding-params = (string) \"1\", "
        "octet-align = (string) \"1\", "
        "crc = (string) { \"0\", \"1\" }, "
        "robust-sorting = (string) \"0\", " "interleaving = (string) \"0\""
        /* following options are not needed for a decoder 
         *
         "mode-set = (int) [ 0, 7 ], "
         "mode-change-period = (int) [ 1, MAX ], "
         "mode-change-neighbor = (boolean) { TRUE, FALSE }, "
         "maxptime = (int) [ 20, MAX ], "
         "ptime = (int) [ 20, MAX ]"
         */
    )
    );

static GstStaticPadTemplate gst_rtp_amr_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "channels = (int) 1," "rate = (int) 8000")
    );

static void gst_rtp_amr_depay_class_init (GstRtpAMRDepayClass * klass);
static void gst_rtp_amr_depay_base_init (GstRtpAMRDepayClass * klass);
static void gst_rtp_amr_depay_init (GstRtpAMRDepay * rtpamrdepay);

static gboolean gst_rtp_amr_depay_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_rtp_amr_depay_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtp_amr_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_amr_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_amr_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtp_amr_depay_get_type (void)
{
  static GType rtpamrdepay_type = 0;

  if (!rtpamrdepay_type) {
    static const GTypeInfo rtpamrdepay_info = {
      sizeof (GstRtpAMRDepayClass),
      (GBaseInitFunc) gst_rtp_amr_depay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_amr_depay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpAMRDepay),
      0,
      (GInstanceInitFunc) gst_rtp_amr_depay_init,
    };

    rtpamrdepay_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpAMRDepay",
        &rtpamrdepay_info, 0);
  }
  return rtpamrdepay_type;
}

static void
gst_rtp_amr_depay_base_init (GstRtpAMRDepayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_amr_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_amr_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_amrdepay_details);
}

static void
gst_rtp_amr_depay_class_init (GstRtpAMRDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtp_amr_depay_set_property;
  gobject_class->get_property = gst_rtp_amr_depay_get_property;

  gstelement_class->change_state = gst_rtp_amr_depay_change_state;
}

static void
gst_rtp_amr_depay_init (GstRtpAMRDepay * rtpamrdepay)
{
  rtpamrdepay->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_amr_depay_src_template, "src");

  gst_element_add_pad (GST_ELEMENT (rtpamrdepay), rtpamrdepay->srcpad);

  rtpamrdepay->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_amr_depay_sink_template,
      "sink");
  gst_pad_set_setcaps_function (rtpamrdepay->sinkpad,
      gst_rtp_amr_depay_sink_setcaps);
  gst_pad_set_chain_function (rtpamrdepay->sinkpad, gst_rtp_amr_depay_chain);
  gst_element_add_pad (GST_ELEMENT (rtpamrdepay), rtpamrdepay->sinkpad);
}

static gboolean
gst_rtp_amr_depay_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *srccaps;
  GstRtpAMRDepay *rtpamrdepay;
  const gchar *params;
  const gchar *str;

  rtpamrdepay = GST_RTP_AMR_DEPAY (GST_OBJECT_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!(str = gst_structure_get_string (structure, "octet-align")))
    rtpamrdepay->octet_align = FALSE;
  else
    rtpamrdepay->octet_align = (atoi (str) == 1);

  if (!(str = gst_structure_get_string (structure, "crc")))
    rtpamrdepay->crc = FALSE;
  else
    rtpamrdepay->crc = (atoi (str) == 1);

  if (rtpamrdepay->crc) {
    /* crc mode implies octet aligned mode */
    rtpamrdepay->octet_align = TRUE;
  }

  if (!(str = gst_structure_get_string (structure, "robust-sorting")))
    rtpamrdepay->robust_sorting = FALSE;
  else
    rtpamrdepay->robust_sorting = (atoi (str) == 1);

  if (rtpamrdepay->robust_sorting) {
    /* robust_sorting mode implies octet aligned mode */
    rtpamrdepay->octet_align = TRUE;
  }

  if (!(str = gst_structure_get_string (structure, "interleaving")))
    rtpamrdepay->interleaving = FALSE;
  else
    rtpamrdepay->interleaving = (atoi (str) == 1);

  if (rtpamrdepay->interleaving) {
    /* interleaving mode implies octet aligned mode */
    rtpamrdepay->octet_align = TRUE;
  }

  if (!(params = gst_structure_get_string (structure, "encoding-params")))
    rtpamrdepay->channels = 1;
  else {
    rtpamrdepay->channels = atoi (params);
  }

  if (!gst_structure_get_int (structure, "clock-rate", &rtpamrdepay->rate))
    rtpamrdepay->rate = 8000;

  /* we require 1 channel, 8000 Hz, octet aligned, no CRC,
   * no robust sorting, no interleaving for now */
  if (rtpamrdepay->channels != 1)
    return FALSE;
  if (rtpamrdepay->rate != 8000)
    return FALSE;
  if (rtpamrdepay->octet_align != TRUE)
    return FALSE;
  if (rtpamrdepay->robust_sorting != FALSE)
    return FALSE;
  if (rtpamrdepay->interleaving != FALSE)
    return FALSE;

  srccaps = gst_caps_new_simple ("audio/AMR",
      "channels", G_TYPE_INT, rtpamrdepay->channels,
      "rate", G_TYPE_INT, rtpamrdepay->rate, NULL);
  gst_pad_set_caps (rtpamrdepay->srcpad, srccaps);
  gst_caps_unref (srccaps);

  rtpamrdepay->negotiated = TRUE;

  return TRUE;
}

/* -1 is invalid */
static gint frame_size[16] = {
  12, 13, 15, 17, 19, 20, 26, 31,
  5, -1, -1, -1, -1, -1, -1, 0
};

static GstFlowReturn
gst_rtp_amr_depay_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpAMRDepay *rtpamrdepay;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  rtpamrdepay = GST_RTP_AMR_DEPAY (GST_OBJECT_PARENT (pad));

  if (!rtpamrdepay->negotiated)
    goto not_negotiated;

  if (!gst_rtp_buffer_validate (buf)) {
    GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
        (NULL), ("AMR RTP packet did not validate"));
    goto bad_packet;
  }

  /* when we get here, 1 channel, 8000 Hz, octet aligned, no CRC, 
   * no robust sorting, no interleaving data is to be depayloaded */
  {
    gint payload_len;
    guint8 *payload, *p, *dp;
    guint32 timestamp;
    guint8 CMR;
    gint i, num_packets, num_nonempty_packets;
    gint amr_len;
    gint ILL, ILP;

    payload_len = gst_rtp_buffer_get_payload_len (buf);

    /* need at least 2 bytes for the header */
    if (payload_len < 2) {
      GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
          (NULL), ("AMR RTP payload too small (%d)", payload_len));
      goto bad_packet;
    }

    payload = gst_rtp_buffer_get_payload (buf);

    /* depay CMR. The CMR is used by the sender to request
     * a new encoding mode.
     *
     *  0 1 2 3 4 5 6 7 
     * +-+-+-+-+-+-+-+-+
     * | CMR   |R|R|R|R|
     * +-+-+-+-+-+-+-+-+
     */
    CMR = (payload[0] & 0xf0) >> 4;

    /* strip CMR header now, pack FT and the data for the decoder */
    payload_len -= 1;
    payload += 1;

    GST_DEBUG_OBJECT (rtpamrdepay, "payload len %d", payload_len);

    if (rtpamrdepay->interleaving) {
      ILL = (payload[0] & 0xf0) >> 4;
      ILP = (payload[0] & 0x0f);

      payload_len -= 1;
      payload += 1;

      if (ILP > ILL) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP wrong interleaving"));
        goto bad_packet;
      }
    }

    /* 
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 
     * +-+-+-+-+-+-+-+-+..
     * |F|  FT   |Q|P|P| more FT..
     * +-+-+-+-+-+-+-+-+..
     */
    /* count number of packets by counting the FTs. Also
     * count number of amr data bytes and number of non-empty
     * packets (this is also the number of CRCs if present). */
    amr_len = 0;
    num_nonempty_packets = 0;
    num_packets = 0;
    for (i = 0; i < payload_len; i++) {
      gint fr_size;
      guint8 FT;

      FT = (payload[i] & 0x78) >> 3;

      fr_size = frame_size[FT];
      GST_DEBUG_OBJECT (rtpamrdepay, "frame size %d", fr_size);
      if (fr_size == -1) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP frame size == -1"));
        goto bad_packet;
      }

      if (fr_size > 0) {
        amr_len += fr_size;
        num_nonempty_packets++;
      }
      num_packets++;

      if ((payload[i] & 0x80) == 0)
        break;
    }

    if (rtpamrdepay->crc) {
      /* data len + CRC len + header bytes should be smaller than payload_len */
      if (num_packets + num_nonempty_packets + amr_len > payload_len) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP wrong length 1"));
        goto bad_packet;
      }
    } else {
      /* data len + header bytes should be smaller than payload_len */
      if (num_packets + amr_len > payload_len) {
        GST_ELEMENT_WARNING (rtpamrdepay, STREAM, DECODE,
            (NULL), ("AMR RTP wrong length 2"));
        goto bad_packet;
      }
    }

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);
    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale_int (timestamp, GST_SECOND, rtpamrdepay->rate);

    /* point to destination */
    p = GST_BUFFER_DATA (outbuf);
    /* point to first data packet */
    dp = payload + num_packets;
    if (rtpamrdepay->crc) {
      /* skip CRC if present */
      dp += num_nonempty_packets;
    }

    for (i = 0; i < num_packets; i++) {
      gint fr_size;

      /* copy FT, clear F bit */
      *p++ = payload[i] & 0x7f;

      fr_size = frame_size[(payload[i] & 0x78) >> 3];
      if (fr_size > 0) {
        /* copy data packet, FIXME, calc CRC here. */
        memcpy (p, dp, fr_size);

        p += fr_size;
        dp += fr_size;
      }
    }
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (rtpamrdepay->srcpad));

    GST_DEBUG ("gst_rtp_amr_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));
    ret = gst_pad_push (rtpamrdepay->srcpad, outbuf);

    gst_buffer_unref (buf);
  }

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (rtpamrdepay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("not negotiated"));
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
bad_packet:
  {
    gst_buffer_unref (buf);
    /* no fatal error */
    return GST_FLOW_OK;
  }
}

static void
gst_rtp_amr_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpAMRDepay *rtpamrdepay;

  rtpamrdepay = GST_RTP_AMR_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_amr_depay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpAMRDepay *rtpamrdepay;

  rtpamrdepay = GST_RTP_AMR_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_amr_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpAMRDepay *rtpamrdepay;
  GstStateChangeReturn ret;

  rtpamrdepay = GST_RTP_AMR_DEPAY (element);

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
gst_rtp_amr_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrdepay",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_DEPAY);
}
