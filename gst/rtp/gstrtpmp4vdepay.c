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
#include "gstrtpmp4vdepay.h"

/* elementfactory information */
static GstElementDetails gst_rtp_mp4vdepay_details = {
  "RTP packet parser",
  "Codec/Depayr/Network",
  "Extracts MPEG4 video from RTP packets (RFC 3016)",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpMP4VDepay signals and args */
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

static GstStaticPadTemplate gst_rtp_mp4v_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg,"
        "mpegversion=(int) 4," "systemstream=(boolean)false")
    );

static GstStaticPadTemplate gst_rtp_mp4v_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP4V-ES\""
        /* All optional parameters
         *
         * "profile-level-id=[1,MAX]"
         * "config=" 
         */
    )
    );


static void gst_rtp_mp4v_depay_class_init (GstRtpMP4VDepayClass * klass);
static void gst_rtp_mp4v_depay_base_init (GstRtpMP4VDepayClass * klass);
static void gst_rtp_mp4v_depay_init (GstRtpMP4VDepay * rtpmp4vdepay);

static gboolean gst_rtp_mp4v_depay_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_rtp_mp4v_depay_chain (GstPad * pad,
    GstBuffer * buffer);

static void gst_rtp_mp4v_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mp4v_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_mp4v_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtp_mp4v_depay_get_type (void)
{
  static GType rtpmp4vdepay_type = 0;

  if (!rtpmp4vdepay_type) {
    static const GTypeInfo rtpmp4vdepay_info = {
      sizeof (GstRtpMP4VDepayClass),
      (GBaseInitFunc) gst_rtp_mp4v_depay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_mp4v_depay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMP4VDepay),
      0,
      (GInstanceInitFunc) gst_rtp_mp4v_depay_init,
    };

    rtpmp4vdepay_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpMP4VDepay",
        &rtpmp4vdepay_info, 0);
  }
  return rtpmp4vdepay_type;
}

static void
gst_rtp_mp4v_depay_base_init (GstRtpMP4VDepayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp4v_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp4v_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mp4vdepay_details);
}

static void
gst_rtp_mp4v_depay_class_init (GstRtpMP4VDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtp_mp4v_depay_set_property;
  gobject_class->get_property = gst_rtp_mp4v_depay_get_property;

  gstelement_class->change_state = gst_rtp_mp4v_depay_change_state;
}

static void
gst_rtp_mp4v_depay_init (GstRtpMP4VDepay * rtpmp4vdepay)
{
  rtpmp4vdepay->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtp_mp4v_depay_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpmp4vdepay), rtpmp4vdepay->srcpad);

  rtpmp4vdepay->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtp_mp4v_depay_sink_template), "sink");
  gst_pad_set_setcaps_function (rtpmp4vdepay->sinkpad,
      gst_rtp_mp4v_depay_setcaps);
  gst_pad_set_chain_function (rtpmp4vdepay->sinkpad, gst_rtp_mp4v_depay_chain);
  gst_element_add_pad (GST_ELEMENT (rtpmp4vdepay), rtpmp4vdepay->sinkpad);
}

static gboolean
gst_rtp_mp4v_depay_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpMP4VDepay *rtpmp4vdepay;
  GstCaps *srccaps;
  const gchar *str;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (GST_OBJECT_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &rtpmp4vdepay->rate))
    rtpmp4vdepay->rate = 90000;

  srccaps = gst_caps_new_simple ("video/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
  gst_pad_set_caps (rtpmp4vdepay->srcpad, srccaps);
  gst_caps_unref (srccaps);

  if ((str = gst_structure_get_string (structure, "config"))) {
    GValue v = { 0 };

    g_value_init (&v, GST_TYPE_BUFFER);
    if (gst_value_deserialize (&v, str)) {
      GstBuffer *buffer;

      buffer = gst_value_get_buffer (&v);
      gst_buffer_ref (buffer);
      g_value_unset (&v);

      gst_buffer_set_caps (buffer, GST_PAD_CAPS (rtpmp4vdepay->srcpad));

      gst_pad_push (rtpmp4vdepay->srcpad, buffer);
    } else {
      g_warning ("cannot convert config to buffer");
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_rtp_mp4v_depay_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpMP4VDepay *rtpmp4vdepay;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (gst_pad_get_parent (pad));

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    guint32 timestamp;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);
    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    gst_adapter_push (rtpmp4vdepay->adapter, outbuf);

    /* if this was the last packet of the VOP, create and push a buffer */
    if (gst_rtp_buffer_get_marker (buf)) {
      guint avail;

      avail = gst_adapter_available (rtpmp4vdepay->adapter);

      outbuf = gst_buffer_new_and_alloc (avail);
      GST_BUFFER_MALLOCDATA (outbuf) =
          gst_adapter_take (rtpmp4vdepay->adapter, avail);
      GST_BUFFER_DATA (outbuf) = GST_BUFFER_MALLOCDATA (outbuf);
      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (rtpmp4vdepay->srcpad));
      GST_BUFFER_TIMESTAMP (outbuf) =
          timestamp * GST_SECOND / rtpmp4vdepay->rate;

      GST_DEBUG ("gst_rtp_mp4v_depay_chain: pushing buffer of size %d",
          GST_BUFFER_SIZE (outbuf));

      ret = gst_pad_push (rtpmp4vdepay->srcpad, outbuf);
    } else {
      ret = GST_FLOW_OK;
    }
    gst_buffer_unref (buf);
  }
  gst_object_unref (rtpmp4vdepay);

  return ret;
bad_packet:
  {
    GST_DEBUG ("Packet did not validate");
    gst_buffer_unref (buf);
    gst_object_unref (rtpmp4vdepay);

    return GST_FLOW_ERROR;
  }
}

static void
gst_rtp_mp4v_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP4VDepay *rtpmp4vdepay;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mp4v_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP4VDepay *rtpmp4vdepay;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mp4v_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpMP4VDepay *rtpmp4vdepay;
  GstStateChangeReturn ret;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      rtpmp4vdepay->adapter = gst_adapter_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpmp4vdepay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_object_unref (rtpmp4vdepay->adapter);
      rtpmp4vdepay->adapter = NULL;
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_mp4v_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp4vdepay",
      GST_RANK_NONE, GST_TYPE_RTP_MP4V_DEPAY);
}
