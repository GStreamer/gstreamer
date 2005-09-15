/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "gstrtpgsmparse.h"

/* elementfactory information */
static GstElementDetails gst_rtp_gsmparse_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts GSM audio from RTP packets",
  "Zeeshan Ali <zak147@yahoo.com>"
};

/* RtpGSMParse signals and args */
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

static GstStaticPadTemplate gst_rtpgsmparse_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) [ 1000, 48000 ]")
    );

static GstStaticPadTemplate gst_rtpgsmparse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static void gst_rtpgsmparse_class_init (GstRtpGSMParseClass * klass);
static void gst_rtpgsmparse_base_init (GstRtpGSMParseClass * klass);
static void gst_rtpgsmparse_init (GstRtpGSMParse * rtpgsmparse);

static GstFlowReturn gst_rtpgsmparse_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpgsmparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpgsmparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpgsmparse_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpgsmparse_get_type (void)
{
  static GType rtpgsmparse_type = 0;

  if (!rtpgsmparse_type) {
    static const GTypeInfo rtpgsmparse_info = {
      sizeof (GstRtpGSMParseClass),
      (GBaseInitFunc) gst_rtpgsmparse_base_init,
      NULL,
      (GClassInitFunc) gst_rtpgsmparse_class_init,
      NULL,
      NULL,
      sizeof (GstRtpGSMParse),
      0,
      (GInstanceInitFunc) gst_rtpgsmparse_init,
    };

    rtpgsmparse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpGSMParse",
        &rtpgsmparse_info, 0);
  }
  return rtpgsmparse_type;
}

static void
gst_rtpgsmparse_base_init (GstRtpGSMParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpgsmparse_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpgsmparse_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_gsmparse_details);
}

static void
gst_rtpgsmparse_class_init (GstRtpGSMParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpgsmparse_set_property;
  gobject_class->get_property = gst_rtpgsmparse_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY,
      g_param_spec_int ("frequency", "frequency", "frequency",
          G_MININT, G_MAXINT, 8000, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_rtpgsmparse_change_state;
}

static void
gst_rtpgsmparse_init (GstRtpGSMParse * rtpgsmparse)
{
  rtpgsmparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpgsmparse_src_template), "src");
  rtpgsmparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpgsmparse_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (rtpgsmparse), rtpgsmparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (rtpgsmparse), rtpgsmparse->sinkpad);
  gst_pad_set_chain_function (rtpgsmparse->sinkpad, gst_rtpgsmparse_chain);

  rtpgsmparse->frequency = 8000;
}

static void
gst_rtpgsm_caps_nego (GstRtpGSMParse * rtpgsmparse)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("audio/x-gsm",
      "rate", G_TYPE_INT, rtpgsmparse->frequency, NULL);

  gst_pad_set_caps (rtpgsmparse->srcpad, caps);
  gst_caps_unref (caps);
}

static GstFlowReturn
gst_rtpgsmparse_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpGSMParse *rtpgsmparse;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint8 pt;

  rtpgsmparse = GST_RTP_GSM_PARSE (gst_pad_get_parent (pad));

  if (GST_PAD_CAPS (rtpgsmparse->srcpad) == NULL) {
    gst_rtpgsm_caps_nego (rtpgsmparse);
  }

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  if ((pt = gst_rtpbuffer_get_payload_type (buf)) != GST_RTP_PAYLOAD_GSM)
    goto bad_payload;

  {
    gint payload_len;
    guint8 *payload;
    guint32 timestamp;

    payload_len = gst_rtpbuffer_get_payload_len (buf);
    payload = gst_rtpbuffer_get_payload (buf);

    timestamp = gst_rtpbuffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / 8000;

    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    GST_DEBUG ("pushing buffer of size %d", GST_BUFFER_SIZE (outbuf));

    gst_buffer_unref (buf);

    ret = gst_pad_push (rtpgsmparse->srcpad, outbuf);
  }

  return ret;

bad_packet:
  {
    GST_DEBUG ("Packet did not validate");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
bad_payload:
  {
    GST_DEBUG ("Unexpected payload type %u", pt);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_rtpgsmparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpGSMParse *rtpgsmparse;

  rtpgsmparse = GST_RTP_GSM_PARSE (object);

  switch (prop_id) {
    case ARG_FREQUENCY:
      rtpgsmparse->frequency = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_rtpgsmparse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpGSMParse *rtpgsmparse;

  rtpgsmparse = GST_RTP_GSM_PARSE (object);

  switch (prop_id) {
    case ARG_FREQUENCY:
      g_value_set_int (value, rtpgsmparse->frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtpgsmparse_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpGSMParse *rtpgsmparse;
  GstStateChangeReturn ret;

  rtpgsmparse = GST_RTP_GSM_PARSE (element);

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
gst_rtpgsmparse_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgsmparse",
      GST_RANK_NONE, GST_TYPE_RTP_GSM_PARSE);
}
