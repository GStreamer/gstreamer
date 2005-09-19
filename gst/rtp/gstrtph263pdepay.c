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
#include "gstrtph263pdec.h"

/* elementfactory information */
static GstElementDetails gst_rtp_h263pdec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts H263+ video from RTP packets (RFC 2429)",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpH263PDec signals and args */
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

static GstStaticPadTemplate gst_rtph263pdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263")
    );

static GstStaticPadTemplate gst_rtph263pdec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 255 ], "
        "clock_rate = (int) 90000, " "encoding_name = (string) \"H263-1998\"")
    );


static void gst_rtph263pdec_class_init (GstRtpH263PDecClass * klass);
static void gst_rtph263pdec_base_init (GstRtpH263PDecClass * klass);
static void gst_rtph263pdec_init (GstRtpH263PDec * rtph263pdec);
static void gst_rtph263pdec_finalize (GObject * object);

static GstFlowReturn gst_rtph263pdec_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtph263pdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtph263pdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtph263pdec_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtph263pdec_get_type (void)
{
  static GType rtph263pdec_type = 0;

  if (!rtph263pdec_type) {
    static const GTypeInfo rtph263pdec_info = {
      sizeof (GstRtpH263PDecClass),
      (GBaseInitFunc) gst_rtph263pdec_base_init,
      NULL,
      (GClassInitFunc) gst_rtph263pdec_class_init,
      NULL,
      NULL,
      sizeof (GstRtpH263PDec),
      0,
      (GInstanceInitFunc) gst_rtph263pdec_init,
    };

    rtph263pdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpH263PDec",
        &rtph263pdec_info, 0);
  }
  return rtph263pdec_type;
}

static void
gst_rtph263pdec_base_init (GstRtpH263PDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263pdec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263pdec_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h263pdec_details);
}

static void
gst_rtph263pdec_class_init (GstRtpH263PDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = gst_rtph263pdec_finalize;

  gobject_class->set_property = gst_rtph263pdec_set_property;
  gobject_class->get_property = gst_rtph263pdec_get_property;

  gstelement_class->change_state = gst_rtph263pdec_change_state;
}

static void
gst_rtph263pdec_init (GstRtpH263PDec * rtph263pdec)
{
  rtph263pdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtph263pdec_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtph263pdec), rtph263pdec->srcpad);

  rtph263pdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtph263pdec_sink_template), "sink");
  gst_pad_set_chain_function (rtph263pdec->sinkpad, gst_rtph263pdec_chain);
  gst_element_add_pad (GST_ELEMENT (rtph263pdec), rtph263pdec->sinkpad);

  rtph263pdec->adapter = gst_adapter_new ();
}

static void
gst_rtph263pdec_finalize (GObject * object)
{
  GstRtpH263PDec *rtph263pdec;

  rtph263pdec = GST_RTP_H263P_DEC (object);

  g_object_unref (rtph263pdec->adapter);
  rtph263pdec->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_rtph263pdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpH263PDec *rtph263pdec;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  /* GstRTPPayload pt; */

  rtph263pdec = GST_RTP_H263P_DEC (GST_OBJECT_PARENT (pad));

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  /*
     if ((pt = gst_rtpbuffer_get_payload_type (buf)) != 0)
     goto bad_payload;
   */

  {
    gint payload_len;
    guint8 *payload;
    gboolean P, V, M;
    guint32 timestamp;
    guint header_len;
    guint8 PLEN;

    payload_len = gst_rtpbuffer_get_payload_len (buf);
    payload = gst_rtpbuffer_get_payload (buf);

    header_len = 2;

    M = gst_rtpbuffer_get_marker (buf);
    P = (payload[0] & 0x04) == 0x04;
    V = (payload[0] & 0x02) == 0x02;
    PLEN = ((payload[0] & 0x1) << 5) | (payload[1] >> 3);

    if (V) {
      header_len++;
    }
    if (PLEN) {
      header_len += PLEN;
    }

    if (P) {
      header_len -= 2;
      payload[header_len] = 0;
      payload[header_len + 1] = 0;
    }

    /* strip off header */
    payload += header_len;
    payload_len -= header_len;

    timestamp = gst_rtpbuffer_get_timestamp (buf);

    if (M) {
      /* frame is completed: append to previous, push it out */
      guint avail;
      guint8 *data;

      avail = gst_adapter_available (rtph263pdec->adapter);

      outbuf = gst_buffer_new_and_alloc (avail + payload_len);

      /* prepend previous data */
      if (avail > 0) {
        data = (guint8 *) gst_adapter_peek (rtph263pdec->adapter, avail);
        memcpy (GST_BUFFER_DATA (outbuf), data, avail);
        gst_adapter_flush (rtph263pdec->adapter, avail);
      }
      memcpy (GST_BUFFER_DATA (outbuf) + avail, payload, payload_len);

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / 90000;
      gst_buffer_set_caps (outbuf,
          (GstCaps *) gst_pad_get_pad_template_caps (rtph263pdec->srcpad));

      ret = gst_pad_push (rtph263pdec->srcpad, outbuf);
    } else {
      /* frame not completed: store in adapter */
      outbuf = gst_buffer_new_and_alloc (payload_len);

      memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

      gst_adapter_push (rtph263pdec->adapter, outbuf);

      ret = GST_FLOW_OK;
    }

    gst_buffer_unref (buf);
  }

  return ret;

bad_packet:
  {
    GST_DEBUG ("Packet does not validate");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
  /*
     bad_payload:
     {
     GST_DEBUG ("Unexpected payload type %u", pt);

     gst_buffer_unref (buf);
     return GST_FLOW_ERROR;
     }
   */
}

static void
gst_rtph263pdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH263PDec *rtph263pdec;

  rtph263pdec = GST_RTP_H263P_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtph263pdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpH263PDec *rtph263pdec;

  rtph263pdec = GST_RTP_H263P_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtph263pdec_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpH263PDec *rtph263pdec;
  GstStateChangeReturn ret;

  rtph263pdec = GST_RTP_H263P_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtph263pdec->adapter);
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
gst_rtph263pdec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263pdec",
      GST_RANK_NONE, GST_TYPE_RTP_H263P_DEC);
}
