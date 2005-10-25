/* GStreamer
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
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
#include "gstrtpg711dec.h"

/* elementfactory information */
static GstElementDetails gst_rtp_g711dec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts PCMU/PCMA audio from RTP packets",
  "Edgard Lima <edgard.lima@indt.org.br>"
};

/* RtpG711Dec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rtpg711dec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) 0, "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"PCMU\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) 8, "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMA\"")

    );

static GstStaticPadTemplate gst_rtpg711dec_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, channels = (int) 1; "
        "audio/x-alaw, channels = (int) 1")
    );

static void gst_rtpg711dec_class_init (GstRtpG711DecClass * klass);
static void gst_rtpg711dec_base_init (GstRtpG711DecClass * klass);
static void gst_rtpg711dec_init (GstRtpG711Dec * rtpg711dec);
static gboolean gst_rtpg711dec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_rtpg711dec_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpg711dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpg711dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpg711dec_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpg711dec_get_type (void)
{
  static GType rtpg711dec_type = 0;

  if (!rtpg711dec_type) {
    static const GTypeInfo rtpg711dec_info = {
      sizeof (GstRtpG711DecClass),
      (GBaseInitFunc) gst_rtpg711dec_base_init,
      NULL,
      (GClassInitFunc) gst_rtpg711dec_class_init,
      NULL,
      NULL,
      sizeof (GstRtpG711Dec),
      0,
      (GInstanceInitFunc) gst_rtpg711dec_init,
    };

    rtpg711dec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpG711Dec",
        &rtpg711dec_info, 0);
  }
  return rtpg711dec_type;
}

static void
gst_rtpg711dec_base_init (GstRtpG711DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg711dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg711dec_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_g711dec_details);
}

static void
gst_rtpg711dec_class_init (GstRtpG711DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpg711dec_set_property;
  gobject_class->get_property = gst_rtpg711dec_get_property;

  gstelement_class->change_state = gst_rtpg711dec_change_state;
}

static void
gst_rtpg711dec_init (GstRtpG711Dec * rtpg711dec)
{
  rtpg711dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpg711dec_src_template), "src");
  rtpg711dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpg711dec_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (rtpg711dec), rtpg711dec->srcpad);

  gst_pad_set_setcaps_function (rtpg711dec->sinkpad,
      gst_rtpg711dec_sink_setcaps);
  gst_element_add_pad (GST_ELEMENT (rtpg711dec), rtpg711dec->sinkpad);
  gst_pad_set_chain_function (rtpg711dec->sinkpad, gst_rtpg711dec_chain);
}

static gboolean
gst_rtpg711dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *srccaps;
  GstRtpG711Dec *rtpg711dec;
  const gchar *enc_name;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  enc_name = gst_structure_get_name (structure);

  enc_name = gst_structure_get_string (structure, "encoding-name");

  if (NULL == enc_name) {
    return FALSE;
  }

  if (0 == strcmp ("PCMU", enc_name)) {
    srccaps = gst_caps_new_simple ("audio/x-mulaw",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
  } else if (0 == strcmp ("PCMA", enc_name)) {
    srccaps = gst_caps_new_simple ("audio/x-alaw",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
  } else {
    return FALSE;
  }

  rtpg711dec = GST_RTP_G711_DEC (GST_OBJECT_PARENT (pad));

  srccaps = gst_caps_new_simple ("audio/x-mulaw",
      "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
  gst_pad_set_caps (rtpg711dec->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

static GstFlowReturn
gst_rtpg711dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpG711Dec *rtpg711dec;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  rtpg711dec = GST_RTP_G711_DEC (gst_pad_get_parent (pad));

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    guint32 timestamp;
    static guint32 firstTS = -1;

    payload_len = gst_rtpbuffer_get_payload_len (buf);
    payload = gst_rtpbuffer_get_payload (buf);

    timestamp = gst_rtpbuffer_get_timestamp (buf);

    if (firstTS == -1) {
      firstTS = gst_rtpbuffer_get_timestamp (buf);
    }
    timestamp = gst_rtpbuffer_get_timestamp (buf) - firstTS;

    outbuf = gst_buffer_new_and_alloc (payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / 8000;

    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    GST_DEBUG ("pushing buffer of size %d", GST_BUFFER_SIZE (outbuf));

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (rtpg711dec->srcpad));
    gst_buffer_unref (buf);

    ret = gst_pad_push (rtpg711dec->srcpad, outbuf);
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
gst_rtpg711dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpG711Dec *rtpg711dec;

  rtpg711dec = GST_RTP_G711_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpg711dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpG711Dec *rtpg711dec;

  rtpg711dec = GST_RTP_G711_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtpg711dec_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpG711Dec *rtpg711dec;
  GstStateChangeReturn ret;

  rtpg711dec = GST_RTP_G711_DEC (element);

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
gst_rtpg711dec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg711dec",
      GST_RANK_NONE, GST_TYPE_RTP_G711_DEC);
}
