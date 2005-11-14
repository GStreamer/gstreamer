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
#include "gstasteriskh263.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#ifdef HAVE_WINSOCK2_H
#  include <winsock2.h>
#endif

#define GST_ASTERISKH263_HEADER_LEN 6

typedef struct _GstAsteriskH263Header
{
  guint32 timestamp;            /* Timestamp */
  guint16 length;               /* Length */
} GstAsteriskH263Header;

#define GST_ASTERISKH263_HEADER_TIMESTAMP(buf) (((GstAsteriskH263Header *)(GST_BUFFER_DATA (buf)))->timestamp)
#define GST_ASTERISKH263_HEADER_LENGTH(buf) (((GstAsteriskH263Header *)(GST_BUFFER_DATA (buf)))->length)

/* elementfactory information */
static GstElementDetails gst_rtp_h263pdec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts H263 video from RTP and encodes in Asterisk H263 format",
  "Neil Stratford <neils@vipadia.com>"
};

/* Asteriskh263 signals and args */
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

static GstStaticPadTemplate gst_asteriskh263_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-asteriskh263")
    );

static GstStaticPadTemplate gst_asteriskh263_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263-1998\"")
    );

static void gst_asteriskh263_class_init (GstAsteriskh263Class * klass);
static void gst_asteriskh263_base_init (GstAsteriskh263Class * klass);
static void gst_asteriskh263_init (GstAsteriskh263 * asteriskh263);
static void gst_asteriskh263_finalize (GObject * object);

static GstFlowReturn gst_asteriskh263_chain (GstPad * pad, GstBuffer * buffer);

static void gst_asteriskh263_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_asteriskh263_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_asteriskh263_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_asteriskh263_get_type (void)
{
  static GType asteriskh263_type = 0;

  if (!asteriskh263_type) {
    static const GTypeInfo asteriskh263_info = {
      sizeof (GstAsteriskh263Class),
      (GBaseInitFunc) gst_asteriskh263_base_init,
      NULL,
      (GClassInitFunc) gst_asteriskh263_class_init,
      NULL,
      NULL,
      sizeof (GstAsteriskh263),
      0,
      (GInstanceInitFunc) gst_asteriskh263_init,
    };

    asteriskh263_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAsteriskh263",
        &asteriskh263_info, 0);
  }
  return asteriskh263_type;
}

static void
gst_asteriskh263_base_init (GstAsteriskh263Class * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asteriskh263_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asteriskh263_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h263pdec_details);
}

static void
gst_asteriskh263_class_init (GstAsteriskh263Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = gst_asteriskh263_finalize;

  gobject_class->set_property = gst_asteriskh263_set_property;
  gobject_class->get_property = gst_asteriskh263_get_property;

  gstelement_class->change_state = gst_asteriskh263_change_state;
}

static void
gst_asteriskh263_init (GstAsteriskh263 * asteriskh263)
{
  asteriskh263->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_asteriskh263_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (asteriskh263), asteriskh263->srcpad);

  asteriskh263->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_asteriskh263_sink_template), "sink");
  gst_pad_set_chain_function (asteriskh263->sinkpad, gst_asteriskh263_chain);
  gst_element_add_pad (GST_ELEMENT (asteriskh263), asteriskh263->sinkpad);

  asteriskh263->adapter = gst_adapter_new ();
}

static void
gst_asteriskh263_finalize (GObject * object)
{
  GstAsteriskh263 *asteriskh263;

  asteriskh263 = GST_ASTERISK_H263 (object);

  g_object_unref (asteriskh263->adapter);
  asteriskh263->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_asteriskh263_chain (GstPad * pad, GstBuffer * buf)
{
  GstAsteriskh263 *asteriskh263;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  asteriskh263 = GST_ASTERISK_H263 (GST_OBJECT_PARENT (pad));

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    gboolean M;
    guint32 timestamp;
    guint32 samples;
    guint16 asterisk_len;

    payload_len = gst_rtpbuffer_get_payload_len (buf);
    payload = gst_rtpbuffer_get_payload (buf);

    M = gst_rtpbuffer_get_marker (buf);
    timestamp = gst_rtpbuffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len +
        GST_ASTERISKH263_HEADER_LEN);

    /* build the asterisk header */
    asterisk_len = payload_len;
    if (M)
      asterisk_len |= 0x8000;
    if (!asteriskh263->lastts)
      asteriskh263->lastts = timestamp;
    samples = timestamp - asteriskh263->lastts;
    asteriskh263->lastts = timestamp;

    GST_ASTERISKH263_HEADER_TIMESTAMP (outbuf) = htonl (samples);
    GST_ASTERISKH263_HEADER_LENGTH (outbuf) = htons (asterisk_len);

    /* copy the data into place */
    memcpy (GST_BUFFER_DATA (outbuf) + GST_ASTERISKH263_HEADER_LEN, payload,
        payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    gst_buffer_set_caps (outbuf,
        (GstCaps *) gst_pad_get_pad_template_caps (asteriskh263->srcpad));

    ret = gst_pad_push (asteriskh263->srcpad, outbuf);

    gst_buffer_unref (buf);
  }

  return ret;

bad_packet:
  {
    GST_DEBUG ("Packet does not validate");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_asteriskh263_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAsteriskh263 *asteriskh263;

  asteriskh263 = GST_ASTERISK_H263 (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_asteriskh263_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAsteriskh263 *asteriskh263;

  asteriskh263 = GST_ASTERISK_H263 (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_asteriskh263_change_state (GstElement * element, GstStateChange transition)
{
  GstAsteriskh263 *asteriskh263;
  GstStateChangeReturn ret;

  asteriskh263 = GST_ASTERISK_H263 (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (asteriskh263->adapter);
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
gst_asteriskh263_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "asteriskh263",
      GST_RANK_NONE, GST_TYPE_ASTERISK_H263);
}
