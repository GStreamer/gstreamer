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

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpmp4gdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpmp4gdepay_debug);
#define GST_CAT_DEFAULT (rtpmp4gdepay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_mp4gdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts MPEG4 elementary streams from RTP packets (RFC 3640)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpMP4GDepay signals and args */
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

static GstStaticPadTemplate gst_rtp_mp4g_depay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg,"
        "mpegversion=(int) 4,"
        "systemstream=(boolean)false;" "audio/mpeg," "mpegversion=(int) 4")
    );

static GstStaticPadTemplate gst_rtp_mp4g_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) { \"video\", \"audio\", \"application\" }, "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [1, MAX ], "
        "encoding-name = (string) \"mpeg4-generic\", "
        /* required string params */
        "streamtype = (string) { \"4\", \"5\" }, "      /* 4 = video, 5 = audio */
        /* "profile-level-id = (string) [1,MAX], " */
        /* "config = (string) [1,MAX]" */
        "mode = (string) { \"generic\", \"CELP-cbr\", \"CELP-vbr\", \"AAC-lbr\", \"AAC-hbr\" } "
        /* Optional general parameters */
        /* "objecttype = (string) [1,MAX], " */
        /* "constantsize = (string) [1,MAX], " *//* constant size of each AU */
        /* "constantduration = (string) [1,MAX], " *//* constant duration of each AU */
        /* "maxdisplacement = (string) [1,MAX], " */
        /* "de-interleavebuffersize = (string) [1,MAX], " */
        /* Optional configuration parameters */
        /* "sizelength = (string) [1, 16], " *//* max 16 bits, should be enough... */
        /* "indexlength = (string) [1, 8], " */
        /* "indexdeltalength = (string) [1, 8], " */
        /* "ctsdeltalength = (string) [1, 64], " */
        /* "dtsdeltalength = (string) [1, 64], " */
        /* "randomaccessindication = (string) {0, 1}, " */
        /* "streamstateindication = (string) [0, 64], " */
        /* "auxiliarydatasizelength = (string) [0, 64]" */ )
    );

GST_BOILERPLATE (GstRtpMP4GDepay, gst_rtp_mp4g_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_mp4g_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mp4g_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_mp4g_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mp4g_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_mp4g_depay_change_state (GstElement *
    element, GstStateChange transition);


static void
gst_rtp_mp4g_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp4g_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp4g_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mp4gdepay_details);
}

static void
gst_rtp_mp4g_depay_class_init (GstRtpMP4GDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_mp4g_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_mp4g_depay_setcaps;

  gobject_class->set_property = gst_rtp_mp4g_depay_set_property;
  gobject_class->get_property = gst_rtp_mp4g_depay_get_property;

  gstelement_class->change_state = gst_rtp_mp4g_depay_change_state;

  GST_DEBUG_CATEGORY_INIT (rtpmp4gdepay_debug, "rtpmp4gdepay", 0,
      "MP4-generic RTP Depayloader");
}

static void
gst_rtp_mp4g_depay_init (GstRtpMP4GDepay * rtpmp4gdepay,
    GstRtpMP4GDepayClass * klass)
{
}

static gboolean
gst_rtp_mp4g_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{

  GstStructure *structure;
  GstRtpMP4GDepay *rtpmp4gdepay;
  GstCaps *srccaps = NULL;
  const gchar *str;
  gint clock_rate = 90000;      /* default */
  gint someint;

  rtpmp4gdepay = GST_RTP_MP4G_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  depayload->clock_rate = clock_rate;

  if ((str = gst_structure_get_string (structure, "media"))) {
    if (strcmp (str, "audio") == 0) {
      srccaps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, NULL);
    } else if (strcmp (str, "video") == 0) {
      srccaps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    }
  }
  if (srccaps == NULL)
    goto unknown_media;

  /* these values are optional and have a default value of 0 (no header) */
  if (!gst_structure_get_int (structure, "sizelength", &someint))
    someint = 0;
  rtpmp4gdepay->sizelength = someint;

  if (!gst_structure_get_int (structure, "indexlength", &someint))
    someint = 0;
  rtpmp4gdepay->indexlength = someint;

  if (!gst_structure_get_int (structure, "indexlength", &someint))
    someint = 0;
  rtpmp4gdepay->indexdeltalength = someint;

  if (!gst_structure_get_int (structure, "ctsdeltalength", &someint))
    someint = 0;
  rtpmp4gdepay->ctsdeltalength = someint;

  if (!gst_structure_get_int (structure, "dtsdeltalength", &someint))
    someint = 0;
  rtpmp4gdepay->dtsdeltalength = someint;

  if (!gst_structure_get_int (structure, "randomaccessindication", &someint))
    someint = 0;
  rtpmp4gdepay->randomaccessindication = someint > 0 ? 1 : 0;

  if (!gst_structure_get_int (structure, "streamstateindication", &someint))
    someint = 0;
  rtpmp4gdepay->streamstateindication = someint;

  if (!gst_structure_get_int (structure, "auxiliarydatasizelength", &someint))
    someint = 0;
  rtpmp4gdepay->auxiliarydatasizelength = someint;

  /* get config string */
  if ((str = gst_structure_get_string (structure, "config"))) {
    GValue v = { 0 };

    g_value_init (&v, GST_TYPE_BUFFER);
    if (gst_value_deserialize (&v, str)) {
      GstBuffer *buffer;

      buffer = gst_value_get_buffer (&v);
      gst_buffer_ref (buffer);
      g_value_unset (&v);

      gst_caps_set_simple (srccaps,
          "codec_data", GST_TYPE_BUFFER, buffer, NULL);
    } else {
      g_warning ("cannot convert config to buffer");
    }
  }

  gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return TRUE;

  /* ERRORS */
unknown_media:
  {
    GST_DEBUG_OBJECT (rtpmp4gdepay, "Unknown media type");
    return FALSE;
  }
}

static GstBuffer *
gst_rtp_mp4g_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMP4GDepay *rtpmp4gdepay;
  GstBuffer *outbuf;

  rtpmp4gdepay = GST_RTP_MP4G_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    guint32 timestamp;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    /* skip header */
    payload += 4;
    payload_len -= 4;

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);
    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    gst_adapter_push (rtpmp4gdepay->adapter, outbuf);

    /* if this was the last packet of the VOP, create and push a buffer */
    if (gst_rtp_buffer_get_marker (buf)) {
      guint avail;

      avail = gst_adapter_available (rtpmp4gdepay->adapter);

      outbuf = gst_buffer_new ();
      GST_BUFFER_SIZE (outbuf) = avail;
      GST_BUFFER_MALLOCDATA (outbuf) =
          gst_adapter_take (rtpmp4gdepay->adapter, avail);
      GST_BUFFER_DATA (outbuf) = GST_BUFFER_MALLOCDATA (outbuf);
      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));
      GST_BUFFER_TIMESTAMP (outbuf) =
          timestamp * GST_SECOND / depayload->clock_rate;

      GST_DEBUG ("gst_rtp_mp4g_depay_chain: pushing buffer of size %d",
          GST_BUFFER_SIZE (outbuf));

      return outbuf;
    } else {
      return NULL;
    }
  }

  return NULL;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtpmp4gdepay, STREAM, DECODE,
        ("Packet did not validate"), (NULL));

    return NULL;
  }
}

static void
gst_rtp_mp4g_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP4GDepay *rtpmp4gdepay;

  rtpmp4gdepay = GST_RTP_MP4G_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mp4g_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP4GDepay *rtpmp4gdepay;

  rtpmp4gdepay = GST_RTP_MP4G_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mp4g_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpMP4GDepay *rtpmp4gdepay;
  GstStateChangeReturn ret;

  rtpmp4gdepay = GST_RTP_MP4G_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      rtpmp4gdepay->adapter = gst_adapter_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpmp4gdepay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_object_unref (rtpmp4gdepay->adapter);
      rtpmp4gdepay->adapter = NULL;
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_mp4g_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp4gdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_MP4G_DEPAY);
}
