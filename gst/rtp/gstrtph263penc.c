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

#include "gstrtph263penc.h"

/* elementfactory information */
static GstElementDetails gst_rtp_h263penc_details = {
  "RTP packet parser",
  "Codec/Encoder/Network",
  "Encodes H263+ video in RTP packets (RFC 2429)",
  "Wim Taymans <wim@fluendo.com>"
};

static GstStaticPadTemplate gst_rtph263penc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263")
    );

static GstStaticPadTemplate gst_rtph263penc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263-1998\"")
    );

static void gst_rtph263penc_class_init (GstRtpH263PEncClass * klass);
static void gst_rtph263penc_base_init (GstRtpH263PEncClass * klass);
static void gst_rtph263penc_init (GstRtpH263PEnc * rtph263penc);
static void gst_rtph263penc_finalize (GObject * object);

static gboolean gst_rtph263penc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtph263penc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtph263penc_get_type (void)
{
  static GType rtph263penc_type = 0;

  if (!rtph263penc_type) {
    static const GTypeInfo rtph263penc_info = {
      sizeof (GstRtpH263PEncClass),
      (GBaseInitFunc) gst_rtph263penc_base_init,
      NULL,
      (GClassInitFunc) gst_rtph263penc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpH263PEnc),
      0,
      (GInstanceInitFunc) gst_rtph263penc_init,
    };

    rtph263penc_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpH263PEnc",
        &rtph263penc_info, 0);
  }
  return rtph263penc_type;
}

static void
gst_rtph263penc_base_init (GstRtpH263PEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263penc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263penc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h263penc_details);
}

static void
gst_rtph263penc_class_init (GstRtpH263PEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gobject_class->finalize = gst_rtph263penc_finalize;

  gstbasertppayload_class->set_caps = gst_rtph263penc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtph263penc_handle_buffer;
}

static void
gst_rtph263penc_init (GstRtpH263PEnc * rtph263penc)
{
  rtph263penc->adapter = gst_adapter_new ();
}

static void
gst_rtph263penc_finalize (GObject * object)
{
  GstRtpH263PEnc *rtph263penc;

  rtph263penc = GST_RTP_H263P_ENC (object);

  g_object_unref (rtph263penc->adapter);
  rtph263penc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtph263penc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "video", TRUE, "H263-1998", 90000);
  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}


static GstFlowReturn
gst_rtph263penc_flush (GstRtpH263PEnc * rtph263penc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gboolean fragmented;

  avail = gst_adapter_available (rtph263penc->adapter);
  if (avail == 0)
    return GST_FLOW_OK;

  fragmented = FALSE;

  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    gint header_len;

    /* FIXME, do better mtu packing, header len etc should be
     * included in this calculation. */
    towrite = MIN (avail, GST_BASE_RTP_PAYLOAD_MTU (rtph263penc));
    /* for fragmented frames we need 2 bytes header, for other
     * frames we must reuse the first 2 bytes of the data as the
     * header */
    header_len = (fragmented ? 2 : 0);
    payload_len = header_len + towrite;

    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
    /* last fragment gets the marker bit set */
    gst_rtpbuffer_set_marker (outbuf, avail > towrite ? 0 : 1);

    payload = gst_rtpbuffer_get_payload (outbuf);

    data = (guint8 *) gst_adapter_peek (rtph263penc->adapter, towrite);
    memcpy (&payload[header_len], data, towrite);

    /*  0                   1
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |   RR    |P|V|   PLEN    |PEBIT|
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    payload[0] = fragmented ? 0x00 : 0x04;
    payload[1] = 0;

    GST_BUFFER_TIMESTAMP (outbuf) = rtph263penc->first_ts;
    gst_adapter_flush (rtph263penc->adapter, towrite);

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtph263penc), outbuf);

    avail -= towrite;
    fragmented = TRUE;
  }

  return ret;
}

static GstFlowReturn
gst_rtph263penc_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstRtpH263PEnc *rtph263penc;
  GstFlowReturn ret;
  guint size;

  rtph263penc = GST_RTP_H263P_ENC (payload);

  size = GST_BUFFER_SIZE (buffer);
  rtph263penc->first_ts = GST_BUFFER_TIMESTAMP (buffer);

  /* we always encode and flush a full picture */
  gst_adapter_push (rtph263penc->adapter, buffer);
  ret = gst_rtph263penc_flush (rtph263penc);

  return ret;
}

gboolean
gst_rtph263penc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263penc",
      GST_RANK_NONE, GST_TYPE_RTP_H263P_ENC);
}
