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
#include "config.h"
#endif
#include <string.h>
#include "gstrtpL16parse.h"
#include "gstrtp-common.h"

/* elementfactory information */
static GstElementDetails gst_rtp_L16parse_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts raw audio from RTP packets",
  "Zeeshan Ali <zak147@yahoo.com>"
};

/* RtpL16Parse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FREQUENCY,
  ARG_PAYLOAD_TYPE,
};

static GstStaticPadTemplate gst_rtpL16parse_src_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( "audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) true, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = (int) [ 1000, 48000 ], "
      "channels = (int) [ 1, 2 ]"
    )
);

static GstStaticPadTemplate gst_rtpL16parse_sink_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
);

static void gst_rtpL16parse_class_init (GstRtpL16ParseClass * klass);
static void gst_rtpL16parse_base_init (GstRtpL16ParseClass * klass);
static void gst_rtpL16parse_init (GstRtpL16Parse * rtpL16parse);

static void gst_rtpL16parse_chain (GstPad * pad, GstData *_data);

static void gst_rtpL16parse_set_property (GObject * object, guint prop_id,
				   const GValue * value, GParamSpec * pspec);
static void gst_rtpL16parse_get_property (GObject * object, guint prop_id,
				   GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_rtpL16parse_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static GType gst_rtpL16parse_get_type (void)
{
  static GType rtpL16parse_type = 0;

  if (!rtpL16parse_type) {
    static const GTypeInfo rtpL16parse_info = {
      sizeof (GstRtpL16ParseClass),
      (GBaseInitFunc) gst_rtpL16parse_base_init,
      NULL,
      (GClassInitFunc) gst_rtpL16parse_class_init,
      NULL,
      NULL,
      sizeof (GstRtpL16Parse),
      0,
      (GInstanceInitFunc) gst_rtpL16parse_init,
    };

    rtpL16parse_type = g_type_register_static (GST_TYPE_ELEMENT, "GstRtpL16Parse", &rtpL16parse_info, 0);
  }
  return rtpL16parse_type;
}

static void
gst_rtpL16parse_base_init (GstRtpL16ParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpL16parse_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpL16parse_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_L16parse_details);
}

static void
gst_rtpL16parse_class_init (GstRtpL16ParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAYLOAD_TYPE, 
		g_param_spec_int ("payload_type", "payload_type", "payload type", 
			G_MININT, G_MAXINT, PAYLOAD_L16_STEREO, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY, 
		g_param_spec_int ("frequency", "frequency", "frequency", 
			G_MININT, G_MAXINT, 44100, G_PARAM_READWRITE));

  gobject_class->set_property = gst_rtpL16parse_set_property;
  gobject_class->get_property = gst_rtpL16parse_get_property;

  gstelement_class->change_state = gst_rtpL16parse_change_state;
}

static void
gst_rtpL16parse_init (GstRtpL16Parse * rtpL16parse)
{
  rtpL16parse->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_rtpL16parse_src_template), "src");
  rtpL16parse->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_rtpL16parse_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (rtpL16parse), rtpL16parse->srcpad);
  gst_element_add_pad (GST_ELEMENT (rtpL16parse), rtpL16parse->sinkpad);
  gst_pad_set_chain_function (rtpL16parse->sinkpad, gst_rtpL16parse_chain);

  rtpL16parse->frequency = 44100;
  rtpL16parse->channels = 2;

  rtpL16parse->payload_type = PAYLOAD_L16_STEREO;
}

void
gst_rtpL16parse_ntohs (GstBuffer *buf)
{
  gint16 *i, *len;

  /* FIXME: is this code correct or even sane at all? */
  i = (gint16 *) GST_BUFFER_DATA(buf); 
  len = i + GST_BUFFER_SIZE (buf) / sizeof (gint16 *);

  for (; i<len; i++) {
      *i = g_ntohs (*i);
  }
}

void
gst_rtpL16_caps_nego (GstRtpL16Parse *rtpL16parse)
{
  GstCaps *caps;

  caps = gst_caps_copy(gst_static_caps_get (&gst_rtpL16parse_src_template.static_caps));

  gst_caps_set_simple (caps,
      "rate", G_TYPE_INT, rtpL16parse->frequency,
      "channel", G_TYPE_INT, rtpL16parse->channels, NULL);

  gst_pad_try_set_caps (rtpL16parse->srcpad, caps);
}

void
gst_rtpL16parse_payloadtype_change (GstRtpL16Parse *rtpL16parse, rtp_payload_t pt)
{
  rtpL16parse->payload_type = pt;
  
  switch (pt) {
	case PAYLOAD_L16_MONO:
      		rtpL16parse->channels = 1;
		break;
	case PAYLOAD_L16_STEREO:
      		rtpL16parse->channels = 2;
		break;
	default:
		g_warning ("unkown payload_t %d\n", pt);
  }

  gst_rtpL16_caps_nego (rtpL16parse);
}

static void
gst_rtpL16parse_chain (GstPad * pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRtpL16Parse *rtpL16parse;
  GstBuffer *outbuf;
  Rtp_Packet packet;
  rtp_payload_t pt;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpL16parse = GST_RTP_L16_PARSE (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpL16parse != NULL);
  g_return_if_fail (GST_IS_RTP_L16_PARSE (rtpL16parse));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);
    gst_pad_event_default (pad, event);
  
    return;
  }

  if (GST_PAD_CAPS (rtpL16parse->srcpad) == NULL) {
    gst_rtpL16_caps_nego (rtpL16parse);
  }

  packet = rtp_packet_new_copy_data (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  pt = rtp_packet_get_payload_type (packet);

  if (pt != rtpL16parse->payload_type) {
	gst_rtpL16parse_payloadtype_change (rtpL16parse, pt);
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = rtp_packet_get_payload_len (packet);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = g_ntohl (rtp_packet_get_timestamp (packet)) * GST_SECOND;

  memcpy (GST_BUFFER_DATA (outbuf), rtp_packet_get_payload (packet), GST_BUFFER_SIZE (outbuf));
        
  GST_DEBUG ("gst_rtpL16parse_chain: pushing buffer of size %d", GST_BUFFER_SIZE(outbuf));

  /* FIXME: According to RFC 1890, this is required, right? */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
     gst_rtpL16parse_ntohs (outbuf);
#endif

  gst_pad_push (rtpL16parse->srcpad, GST_DATA (outbuf));

  rtp_packet_free (packet);
  gst_buffer_unref (buf);
}

static void
gst_rtpL16parse_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpL16Parse *rtpL16parse;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_PARSE (object));
  rtpL16parse = GST_RTP_L16_PARSE (object);

  switch (prop_id) {
    case ARG_PAYLOAD_TYPE:
      gst_rtpL16parse_payloadtype_change (rtpL16parse, g_value_get_int (value));
      break;
    case ARG_FREQUENCY:
      rtpL16parse->frequency = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_rtpL16parse_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpL16Parse *rtpL16parse;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_PARSE (object));
  rtpL16parse = GST_RTP_L16_PARSE (object);

  switch (prop_id) {
    case ARG_PAYLOAD_TYPE:
      g_value_set_int (value, rtpL16parse->payload_type);
      break;
    case ARG_FREQUENCY:
      g_value_set_int (value, rtpL16parse->frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpL16parse_change_state (GstElement * element)
{
  GstRtpL16Parse *rtpL16parse;

  g_return_val_if_fail (GST_IS_RTP_L16_PARSE (element), GST_STATE_FAILURE);

  rtpL16parse = GST_RTP_L16_PARSE (element);

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
gst_rtpL16parse_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16parse",
			       GST_RANK_NONE, GST_TYPE_RTP_L16_PARSE);
}
