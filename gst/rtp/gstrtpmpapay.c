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

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpmpaenc.h"

/* elementfactory information */
static GstElementDetails gst_rtp_mpaenc_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Encode MPEG audio as RTP packets (RFC 2038)",
  "Wim Taymans <wim@fluendo.com>"
};

static GstStaticPadTemplate gst_rtpmpaenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
    );

static GstStaticPadTemplate gst_rtpmpaenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPA\"")
    );

static void gst_rtpmpaenc_class_init (GstRtpMPAEncClass * klass);
static void gst_rtpmpaenc_base_init (GstRtpMPAEncClass * klass);
static void gst_rtpmpaenc_init (GstRtpMPAEnc * rtpmpaenc);
static void gst_rtpmpaenc_finalize (GObject * object);

static gboolean gst_rtpmpaenc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtpmpaenc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtpmpaenc_get_type (void)
{
  static GType rtpmpaenc_type = 0;

  if (!rtpmpaenc_type) {
    static const GTypeInfo rtpmpaenc_info = {
      sizeof (GstRtpMPAEncClass),
      (GBaseInitFunc) gst_rtpmpaenc_base_init,
      NULL,
      (GClassInitFunc) gst_rtpmpaenc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMPAEnc),
      0,
      (GInstanceInitFunc) gst_rtpmpaenc_init,
    };

    rtpmpaenc_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpMPAEnc",
        &rtpmpaenc_info, 0);
  }
  return rtpmpaenc_type;
}

static void
gst_rtpmpaenc_base_init (GstRtpMPAEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmpaenc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmpaenc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpaenc_details);
}

static void
gst_rtpmpaenc_class_init (GstRtpMPAEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gobject_class->finalize = gst_rtpmpaenc_finalize;

  gstbasertppayload_class->set_caps = gst_rtpmpaenc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtpmpaenc_handle_buffer;
}

static void
gst_rtpmpaenc_init (GstRtpMPAEnc * rtpmpaenc)
{
  rtpmpaenc->adapter = gst_adapter_new ();
}

static void
gst_rtpmpaenc_finalize (GObject * object)
{
  GstRtpMPAEnc *rtpmpaenc;

  rtpmpaenc = GST_RTP_MPA_ENC (object);

  g_object_unref (rtpmpaenc->adapter);
  rtpmpaenc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtpmpaenc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "audio", TRUE, "MPA", 90000);
  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtpmpaenc_flush (GstRtpMPAEnc * rtpmpaenc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint16 frag_offset;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to split the MPA data
   * over multiple packets. The frag_offset in each packet header
   * needs to be updated with the position in the MPA frame. */
  avail = gst_adapter_available (rtpmpaenc->adapter);

  ret = GST_FLOW_OK;

  frag_offset = 0;
  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtpbuffer_calc_packet_len (4 + avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpmpaenc));

    /* this is the payload length */
    payload_len = gst_rtpbuffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);

    payload_len -= 4;

    gst_rtpbuffer_set_payload_type (outbuf, GST_RTP_PAYLOAD_MPA);

    /*
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |             MBZ               |          Frag_offset          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
     */
    payload = gst_rtpbuffer_get_payload (outbuf);
    payload[0] = 0;
    payload[1] = 0;
    payload[2] = frag_offset >> 8;
    payload[3] = frag_offset & 0xff;

    data = (guint8 *) gst_adapter_peek (rtpmpaenc->adapter, payload_len);
    memcpy (&payload[4], data, payload_len);
    gst_adapter_flush (rtpmpaenc->adapter, payload_len);

    avail -= payload_len;
    frag_offset += payload_len;

    if (avail == 0)
      gst_rtpbuffer_set_marker (outbuf, TRUE);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmpaenc->first_ts;
    GST_BUFFER_DURATION (outbuf) = rtpmpaenc->duration;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpmpaenc), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtpmpaenc_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpMPAEnc *rtpmpaenc;
  GstFlowReturn ret;
  guint size, avail;
  guint packet_len;
  GstClockTime duration;

  rtpmpaenc = GST_RTP_MPA_ENC (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  avail = gst_adapter_available (rtpmpaenc->adapter);
  if (avail == 0) {
    rtpmpaenc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpmpaenc->duration = 0;
  }

  /* get packet length of previous data and this new data, 
   * payload length includes a 4 byte header */
  packet_len = gst_rtpbuffer_calc_packet_len (4 + avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpmpaenc->duration + duration)) {
    ret = gst_rtpmpaenc_flush (rtpmpaenc);
    rtpmpaenc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpmpaenc->duration = 0;
  } else {
    ret = GST_FLOW_OK;
  }

  gst_adapter_push (rtpmpaenc->adapter, buffer);
  rtpmpaenc->duration += duration;

  return ret;
}

gboolean
gst_rtpmpaenc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpaenc",
      GST_RANK_NONE, GST_TYPE_RTP_MPA_ENC);
}
