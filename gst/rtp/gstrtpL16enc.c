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
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include "gstrtpL16enc.h"

/* elementfactory information */
static GstElementDetails gst_rtpL16enc_details = {
  "RTP RAW Audio Encoder",
  "RtpL16Enc",
  "LGPL",
  "Encodes Raw Audio into an RTP packet",
  VERSION,
  "Zeeshan Ali <zak147@yahoo.com>",
  "(C) 2003",
};

/* RtpL16Enc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  /* FILL ME */
  ARG_0,
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
		"sink",
		GST_PAD_SINK,
	        GST_PAD_ALWAYS,
		GST_CAPS_NEW (
			"audio_raw",
			"audio/raw",
			"format",	GST_PROPS_STRING ("int"),
			"law", 		GST_PROPS_INT (0),
			"endianness", 	GST_PROPS_INT (G_BYTE_ORDER), 
			"signed",	GST_PROPS_BOOLEAN (TRUE), 
			"width",	GST_PROPS_INT (16), 
			"depth",	GST_PROPS_INT (16), 
			"rate",		GST_PROPS_INT_RANGE (1000, 48000),
			"channels", 	GST_PROPS_INT_RANGE (1, 2)
		)
);

GST_PAD_TEMPLATE_FACTORY (src_factory,
		"src",
		GST_PAD_SRC,
	        GST_PAD_ALWAYS,
		GST_CAPS_NEW (
			"rtp",
			"application/x-rtp",
			NULL)
);

static void gst_rtpL16enc_class_init (GstRtpL16EncClass * klass);
static void gst_rtpL16enc_init (GstRtpL16Enc * rtpL16enc);
static void gst_rtpL16enc_chain (GstPad * pad, GstBuffer * buf);
static void gst_rtpL16enc_set_property (GObject * object, guint prop_id,
				   const GValue * value, GParamSpec * pspec);
static void gst_rtpL16enc_get_property (GObject * object, guint prop_id,
				   GValue * value, GParamSpec * pspec);
static GstPadLinkReturn gst_rtpL16enc_sinkconnect (GstPad * pad, GstCaps * caps);
static GstElementStateReturn gst_rtpL16enc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static GType gst_rtpL16enc_get_type (void)
{
  static GType rtpL16enc_type = 0;

  if (!rtpL16enc_type) {
    static const GTypeInfo rtpL16enc_info = {
      sizeof (GstRtpL16EncClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_rtpL16enc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpL16Enc),
      0,
      (GInstanceInitFunc) gst_rtpL16enc_init,
    };

    rtpL16enc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstRtpL16Enc", &rtpL16enc_info, 0);
  }
  return rtpL16enc_type;
}

static void
gst_rtpL16enc_class_init (GstRtpL16EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpL16enc_set_property;
  gobject_class->get_property = gst_rtpL16enc_get_property;

  gstelement_class->change_state = gst_rtpL16enc_change_state;
}

static void
gst_rtpL16enc_init (GstRtpL16Enc * rtpL16enc)
{
  rtpL16enc->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  rtpL16enc->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (rtpL16enc), rtpL16enc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (rtpL16enc), rtpL16enc->srcpad);
  gst_pad_set_chain_function (rtpL16enc->sinkpad, gst_rtpL16enc_chain);
  gst_pad_set_link_function (rtpL16enc->sinkpad, gst_rtpL16enc_sinkconnect);

  rtpL16enc->frequency = 44100;
  rtpL16enc->channels = 2;

  rtpL16enc->next_time = 0; 
  rtpL16enc->time_interval = 0;

  rtpL16enc->seq = 0;
  rtpL16enc->ssrc = random ();
}

static GstPadLinkReturn
gst_rtpL16enc_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstRtpL16Enc *rtpL16enc;

  rtpL16enc = GST_RTP_L16_ENC (gst_pad_get_parent (pad));

  gst_caps_get_int (caps, "rate", &rtpL16enc->frequency);
  gst_caps_get_int (caps, "channels", &rtpL16enc->channels);

  /* Pre-calculate what we can */
  rtpL16enc->time_interval = GST_SECOND / (2 * rtpL16enc->channels * rtpL16enc->frequency);

  return GST_PAD_LINK_OK;
}


void
gst_rtpL16enc_htons (GstBuffer *buf)
{
  guint16 *i, *len;

  /* FIXME: is this code correct or even sane at all? */
  i = (guint16 *) GST_BUFFER_DATA(buf); 
  len = i + GST_BUFFER_SIZE (buf) / sizeof (guint16 *);

  for (; i<len; i++) {
      *i = g_htons (*i);
  }
}

static void
gst_rtpL16enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpL16Enc *rtpL16enc;
  GstBuffer *outbuf;
  Rtp_Packet packet;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpL16enc = GST_RTP_L16_ENC (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpL16enc != NULL);
  g_return_if_fail (GST_IS_RTP_L16_ENC (rtpL16enc));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
	GST_DEBUG (GST_CAT_EVENT, "discont"); 
        rtpL16enc->next_time = 0;
        gst_pad_event_default (pad, event);
	return;
      default:
        gst_pad_event_default (pad, event);
	return;
    }
  }

  /* We only need the header */
  packet = rtp_packet_new_allocate (0, 0, 0);

  rtp_packet_set_csrc_count (packet, 0);
  rtp_packet_set_extension (packet, 0);
  rtp_packet_set_padding (packet, 0);
  rtp_packet_set_version (packet, RTP_VERSION);
  rtp_packet_set_marker (packet, 0);
  rtp_packet_set_ssrc (packet, g_htonl (rtpL16enc->ssrc));
  rtp_packet_set_seq (packet, g_htons (rtpL16enc->seq));
  rtp_packet_set_timestamp (packet, g_htonl ((guint32) rtpL16enc->next_time / GST_SECOND));

  if (rtpL16enc->channels == 1) {
     rtp_packet_set_payload_type (packet, (guint8) PAYLOAD_L16_MONO);
  }

  else {
     rtp_packet_set_payload_type (packet, (guint8) PAYLOAD_L16_STEREO);
  }

  /* FIXME: According to RFC 1890, this is required, right? */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
     gst_rtpL16enc_htons (buf);
#endif

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = rtp_packet_get_packet_len (packet) + GST_BUFFER_SIZE (buf);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = rtpL16enc->next_time;

  memcpy (GST_BUFFER_DATA (outbuf), packet->data, rtp_packet_get_packet_len (packet));
  memcpy (GST_BUFFER_DATA (outbuf) + rtp_packet_get_packet_len(packet), GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  GST_DEBUG (0,"gst_rtpL16enc_chain: pushing buffer of size %d", GST_BUFFER_SIZE(outbuf));
  gst_pad_push (rtpL16enc->srcpad, outbuf);

  ++rtpL16enc->seq;
  rtpL16enc->next_time += rtpL16enc->time_interval * GST_BUFFER_SIZE (buf);
  
  rtp_packet_free (packet);
  gst_buffer_unref (buf);
}

static void
gst_rtpL16enc_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpL16Enc *rtpL16enc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_ENC (object));
  rtpL16enc = GST_RTP_L16_ENC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_rtpL16enc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpL16Enc *rtpL16enc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_ENC (object));
  rtpL16enc = GST_RTP_L16_ENC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpL16enc_change_state (GstElement * element)
{
  GstRtpL16Enc *rtpL16enc;

  g_return_val_if_fail (GST_IS_RTP_L16_ENC (element), GST_STATE_FAILURE);

  rtpL16enc = GST_RTP_L16_ENC (element);

  GST_DEBUG (0, "state pending %d\n", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
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
gst_rtpL16enc_plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *rtpL16enc;

  rtpL16enc = gst_element_factory_new ("rtpL16enc", GST_TYPE_RTP_L16_ENC, &gst_rtpL16enc_details);
  g_return_val_if_fail (rtpL16enc != NULL, FALSE);

  gst_element_factory_add_pad_template (rtpL16enc, GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_factory_add_pad_template (rtpL16enc, GST_PAD_TEMPLATE_GET (src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (rtpL16enc));

  return TRUE;
}
