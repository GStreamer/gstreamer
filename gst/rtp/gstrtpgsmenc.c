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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include "gstrtpgsmenc.h"

/* elementfactory information */
static GstElementDetails gst_rtpgsmenc_details = {
  "RTP GSM Audio Encoder",
  "Codec/Network",
  "LGPL",
  "Encodes GSM audio into an RTP packet",
  VERSION,
  "Zeeshan Ali <zak147@yahoo.com>",
  "(C) 2003",
};

/* RtpGSMEnc signals and args */
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
    			"gsm_gsm",
    			"audio/x-gsm",
      			"rate",       GST_PROPS_INT_RANGE (1000, 48000)
		)
)

GST_PAD_TEMPLATE_FACTORY (src_factory,
		"src",
		GST_PAD_SRC,
	        GST_PAD_ALWAYS,
		GST_CAPS_NEW (
			"rtp",
			"application/x-rtp",
			NULL)
)

static void gst_rtpgsmenc_class_init (GstRtpGSMEncClass * klass);
static void gst_rtpgsmenc_init (GstRtpGSMEnc * rtpgsmenc);
static void gst_rtpgsmenc_chain (GstPad * pad, GstData *_data);
static void gst_rtpgsmenc_set_property (GObject * object, guint prop_id,
				   const GValue * value, GParamSpec * pspec);
static void gst_rtpgsmenc_get_property (GObject * object, guint prop_id,
				   GValue * value, GParamSpec * pspec);
static GstPadLinkReturn gst_rtpgsmenc_sinkconnect (GstPad * pad, GstCaps * caps);
static GstElementStateReturn gst_rtpgsmenc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static GType gst_rtpgsmenc_get_type (void)
{
  static GType rtpgsmenc_type = 0;

  if (!rtpgsmenc_type) {
    static const GTypeInfo rtpgsmenc_info = {
      sizeof (GstRtpGSMEncClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_rtpgsmenc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpGSMEnc),
      0,
      (GInstanceInitFunc) gst_rtpgsmenc_init,
    };

    rtpgsmenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstRtpGSMEnc", &rtpgsmenc_info, 0);
  }
  return rtpgsmenc_type;
}

static void
gst_rtpgsmenc_class_init (GstRtpGSMEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpgsmenc_set_property;
  gobject_class->get_property = gst_rtpgsmenc_get_property;

  gstelement_class->change_state = gst_rtpgsmenc_change_state;
}

static void
gst_rtpgsmenc_init (GstRtpGSMEnc * rtpgsmenc)
{
  rtpgsmenc->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  rtpgsmenc->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (rtpgsmenc), rtpgsmenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (rtpgsmenc), rtpgsmenc->srcpad);
  gst_pad_set_chain_function (rtpgsmenc->sinkpad, gst_rtpgsmenc_chain);
  gst_pad_set_link_function (rtpgsmenc->sinkpad, gst_rtpgsmenc_sinkconnect);

  rtpgsmenc->frequency = 8000;

  rtpgsmenc->next_time = 0; 
  rtpgsmenc->time_interval = 0;

  rtpgsmenc->seq = 0;
  rtpgsmenc->ssrc = random ();
}

static GstPadLinkReturn
gst_rtpgsmenc_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstRtpGSMEnc *rtpgsmenc;

  rtpgsmenc = GST_RTP_GSM_ENC (gst_pad_get_parent (pad));

  gst_caps_get_int (caps, "rate", &rtpgsmenc->frequency);

  /* Pre-calculate what we can */
  rtpgsmenc->time_interval = GST_SECOND / (2 * rtpgsmenc->frequency);

  return GST_PAD_LINK_OK;
}


void
gst_rtpgsmenc_htons (GstBuffer *buf)
{
  gint16 *i, *len;

  /* FIXME: is this code correct or even sane at all? */
  i = (gint16 *) GST_BUFFER_DATA(buf); 
  len = i + GST_BUFFER_SIZE (buf) / sizeof (gint16 *);

  for (; i<len; i++) {
      *i = g_htons (*i);
  }
}

static void
gst_rtpgsmenc_chain (GstPad * pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRtpGSMEnc *rtpgsmenc;
  GstBuffer *outbuf;
  Rtp_Packet packet;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpgsmenc = GST_RTP_GSM_ENC (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpgsmenc != NULL);
  g_return_if_fail (GST_IS_RTP_GSM_ENC (rtpgsmenc));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
	GST_DEBUG ("discont"); 
        rtpgsmenc->next_time = 0;
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
  rtp_packet_set_ssrc (packet, g_htonl (rtpgsmenc->ssrc));
  rtp_packet_set_seq (packet, g_htons (rtpgsmenc->seq));
  rtp_packet_set_timestamp (packet, g_htonl ((guint32) rtpgsmenc->next_time / GST_SECOND));
  rtp_packet_set_payload_type (packet, (guint8) PAYLOAD_GSM);

  /* FIXME: According to RFC 1890, this is required, right? */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  gst_rtpgsmenc_htons (buf);
#endif

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = rtp_packet_get_packet_len (packet) + GST_BUFFER_SIZE (buf);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = rtpgsmenc->next_time;

  memcpy (GST_BUFFER_DATA (outbuf), packet->data, rtp_packet_get_packet_len (packet));
  memcpy (GST_BUFFER_DATA (outbuf) + rtp_packet_get_packet_len(packet), GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  GST_DEBUG ("gst_rtpgsmenc_chain: pushing buffer of size %d", GST_BUFFER_SIZE(outbuf));
  gst_pad_push (rtpgsmenc->srcpad, GST_DATA (outbuf));

  ++rtpgsmenc->seq;
  rtpgsmenc->next_time += rtpgsmenc->time_interval * GST_BUFFER_SIZE (buf);
  
  rtp_packet_free (packet);
  gst_buffer_unref (buf);
}

static void
gst_rtpgsmenc_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpGSMEnc *rtpgsmenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_GSM_ENC (object));
  rtpgsmenc = GST_RTP_GSM_ENC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_rtpgsmenc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpGSMEnc *rtpgsmenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_GSM_ENC (object));
  rtpgsmenc = GST_RTP_GSM_ENC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpgsmenc_change_state (GstElement * element)
{
  GstRtpGSMEnc *rtpgsmenc;

  g_return_val_if_fail (GST_IS_RTP_GSM_ENC (element), GST_STATE_FAILURE);

  rtpgsmenc = GST_RTP_GSM_ENC (element);

  GST_DEBUG ("state pending %d\n", GST_STATE_PENDING (element));

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
gst_rtpgsmenc_plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *rtpgsmenc;

  rtpgsmenc = gst_element_factory_new ("rtpgsmenc", GST_TYPE_RTP_GSM_ENC, &gst_rtpgsmenc_details);
  g_return_val_if_fail (rtpgsmenc != NULL, FALSE);

  gst_element_factory_add_pad_template (rtpgsmenc, GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_factory_add_pad_template (rtpgsmenc, GST_PAD_TEMPLATE_GET (src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (rtpgsmenc));

  return TRUE;
}
