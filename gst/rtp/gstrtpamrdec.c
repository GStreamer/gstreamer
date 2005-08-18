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
#include "gstrtpamrdec.h"

/* elementfactory information */
static GstElementDetails gst_rtp_amrdec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts MPEG audio from RTP packets",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpAMRDec signals and args */
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

static GstStaticPadTemplate gst_rtpamrdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
    );

static GstStaticPadTemplate gst_rtpamrdec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static void gst_rtpamrdec_class_init (GstRtpAMRDecClass * klass);
static void gst_rtpamrdec_base_init (GstRtpAMRDecClass * klass);
static void gst_rtpamrdec_init (GstRtpAMRDec * rtpamrdec);

static GstFlowReturn gst_rtpamrdec_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpamrdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpamrdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_rtpamrdec_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpamrdec_get_type (void)
{
  static GType rtpamrdec_type = 0;

  if (!rtpamrdec_type) {
    static const GTypeInfo rtpamrdec_info = {
      sizeof (GstRtpAMRDecClass),
      (GBaseInitFunc) gst_rtpamrdec_base_init,
      NULL,
      (GClassInitFunc) gst_rtpamrdec_class_init,
      NULL,
      NULL,
      sizeof (GstRtpAMRDec),
      0,
      (GInstanceInitFunc) gst_rtpamrdec_init,
    };

    rtpamrdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpAMRDec",
        &rtpamrdec_info, 0);
  }
  return rtpamrdec_type;
}

static void
gst_rtpamrdec_base_init (GstRtpAMRDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpamrdec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpamrdec_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_amrdec_details);
}

static void
gst_rtpamrdec_class_init (GstRtpAMRDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpamrdec_set_property;
  gobject_class->get_property = gst_rtpamrdec_get_property;

  gstelement_class->change_state = gst_rtpamrdec_change_state;
}

static void
gst_rtpamrdec_init (GstRtpAMRDec * rtpamrdec)
{
  rtpamrdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpamrdec_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpamrdec), rtpamrdec->srcpad);

  rtpamrdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpamrdec_sink_template), "sink");
  gst_pad_set_chain_function (rtpamrdec->sinkpad, gst_rtpamrdec_chain);
  gst_element_add_pad (GST_ELEMENT (rtpamrdec), rtpamrdec->sinkpad);
}

static GstFlowReturn
gst_rtpamrdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpAMRDec *rtpamrdec;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  rtpamrdec = GST_RTP_AMR_DEC (GST_OBJECT_PARENT (pad));

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    guint16 frag_offset;
    guint32 timestamp;

    payload_len = gst_rtpbuffer_get_payload_len (buf);
    payload = gst_rtpbuffer_get_payload (buf);

    frag_offset = (payload[2] << 8) | payload[3];

    /* strip off header */
    payload_len -= 4;
    payload += 4;

    timestamp = gst_rtpbuffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);

    //GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / 90000;

    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    GST_DEBUG ("gst_rtpamrdec_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    gst_buffer_unref (buf);

    ret = gst_pad_push (rtpamrdec->srcpad, outbuf);
  }

  return ret;

bad_packet:
  {
    GST_DEBUG ("Packet did not validate");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_rtpamrdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpAMRDec *rtpamrdec;

  rtpamrdec = GST_RTP_AMR_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpamrdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpAMRDec *rtpamrdec;

  rtpamrdec = GST_RTP_AMR_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpamrdec_change_state (GstElement * element)
{
  GstRtpAMRDec *rtpamrdec;
  gint transition;
  GstElementStateReturn ret;

  rtpamrdec = GST_RTP_AMR_DEC (element);
  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtpamrdec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrdec",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_DEC);
}
