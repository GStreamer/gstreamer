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
#include "gstrtpgsmparse.h"
#include "gstrtp-common.h"

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

static void gst_rtpgsmparse_chain (GstPad * pad, GstData * _data);

static void gst_rtpgsmparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpgsmparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_rtpgsmparse_change_state (GstElement *
    element);

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

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY,
      g_param_spec_int ("frequency", "frequency", "frequency",
	  G_MININT, G_MAXINT, 8000, G_PARAM_READWRITE));

  gobject_class->set_property = gst_rtpgsmparse_set_property;
  gobject_class->get_property = gst_rtpgsmparse_get_property;

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

void
gst_rtpgsmparse_ntohs (GstBuffer * buf)
{
  gint16 *i, *len;

  /* FIXME: is this code correct or even sane at all? */
  i = (gint16 *) GST_BUFFER_DATA (buf);
  len = i + GST_BUFFER_SIZE (buf) / sizeof (gint16 *);

  for (; i < len; i++) {
    *i = g_ntohs (*i);
  }
}

void
gst_rtpgsm_caps_nego (GstRtpGSMParse * rtpgsmparse)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("audio/x-gsm",
      "rate", G_TYPE_INT, rtpgsmparse->frequency);

  gst_pad_try_set_caps (rtpgsmparse->srcpad, caps);
}

static void
gst_rtpgsmparse_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRtpGSMParse *rtpgsmparse;
  GstBuffer *outbuf;
  Rtp_Packet packet;
  rtp_payload_t pt;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpgsmparse = GST_RTP_GSM_PARSE (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpgsmparse != NULL);
  g_return_if_fail (GST_IS_RTP_GSM_PARSE (rtpgsmparse));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    gst_pad_event_default (pad, event);

    return;
  }

  if (GST_PAD_CAPS (rtpgsmparse->srcpad) == NULL) {
    gst_rtpgsm_caps_nego (rtpgsmparse);
  }

  packet =
      rtp_packet_new_copy_data (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  pt = rtp_packet_get_payload_type (packet);

  if (pt != PAYLOAD_GSM) {
    g_warning ("Unexpected paload type %u\n", pt);
    rtp_packet_free (packet);
    gst_buffer_unref (buf);
    return;
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = rtp_packet_get_payload_len (packet);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) =
      g_ntohl (rtp_packet_get_timestamp (packet)) * GST_SECOND;

  memcpy (GST_BUFFER_DATA (outbuf), rtp_packet_get_payload (packet),
      GST_BUFFER_SIZE (outbuf));

  GST_DEBUG ("gst_rtpgsmparse_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

/* FIXME: According to RFC 1890, this is required, right? */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  gst_rtpgsmparse_ntohs (outbuf);
#endif

  gst_pad_push (rtpgsmparse->srcpad, GST_DATA (outbuf));

  rtp_packet_free (packet);
  gst_buffer_unref (buf);
}

static void
gst_rtpgsmparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpGSMParse *rtpgsmparse;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_GSM_PARSE (object));
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

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_GSM_PARSE (object));
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

static GstElementStateReturn
gst_rtpgsmparse_change_state (GstElement * element)
{
  GstRtpGSMParse *rtpgsmparse;

  g_return_val_if_fail (GST_IS_RTP_GSM_PARSE (element), GST_STATE_FAILURE);

  rtpgsmparse = GST_RTP_GSM_PARSE (element);

  GST_DEBUG ("state pending %d\n", GST_STATE_PENDING (element));

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_rtpgsmparse_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgsmparse",
      GST_RANK_NONE, GST_TYPE_RTP_GSM_PARSE);
}
