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

#include <string.h>
#include <stdlib.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpmp4gdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpmp4gdepay_debug);
#define GST_CAT_DEFAULT (rtpmp4gdepay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_mp4gdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts MPEG4 elementary streams from RTP packets (RFC 3640)",
    "Wim Taymans <wim@fluendo.com>");

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
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], "
        "encoding-name = (string) \"MPEG4-GENERIC\", "
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

static void gst_rtp_mp4g_depay_finalize (GObject * object);

static gboolean gst_rtp_mp4g_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mp4g_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

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

  gobject_class->finalize = gst_rtp_mp4g_depay_finalize;

  gstelement_class->change_state = gst_rtp_mp4g_depay_change_state;

  gstbasertpdepayload_class->process = gst_rtp_mp4g_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_mp4g_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpmp4gdepay_debug, "rtpmp4gdepay", 0,
      "MP4-generic RTP Depayloader");
}

static void
gst_rtp_mp4g_depay_init (GstRtpMP4GDepay * rtpmp4gdepay,
    GstRtpMP4GDepayClass * klass)
{
  rtpmp4gdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_mp4g_depay_finalize (GObject * object)
{
  GstRtpMP4GDepay *rtpmp4gdepay;

  rtpmp4gdepay = GST_RTP_MP4G_DEPAY (object);

  g_object_unref (rtpmp4gdepay->adapter);
  rtpmp4gdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gint
gst_rtp_mp4g_depay_parse_int (GstStructure * structure, const gchar * field,
    gint def)
{
  const gchar *str;
  gint res;

  if ((str = gst_structure_get_string (structure, field)))
    return atoi (str);

  if (gst_structure_get_int (structure, field, &res))
    return res;

  return def;
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
  rtpmp4gdepay->sizelength =
      gst_rtp_mp4g_depay_parse_int (structure, "sizelength", 0);
  rtpmp4gdepay->indexlength =
      gst_rtp_mp4g_depay_parse_int (structure, "indexlength", 0);
  rtpmp4gdepay->indexdeltalength =
      gst_rtp_mp4g_depay_parse_int (structure, "indexdeltalength", 0);
  rtpmp4gdepay->ctsdeltalength =
      gst_rtp_mp4g_depay_parse_int (structure, "ctsdeltalength", 0);
  rtpmp4gdepay->dtsdeltalength =
      gst_rtp_mp4g_depay_parse_int (structure, "dtsdeltalength", 0);
  someint =
      gst_rtp_mp4g_depay_parse_int (structure, "randomaccessindication", 0);
  rtpmp4gdepay->randomaccessindication = someint > 0 ? 1 : 0;
  rtpmp4gdepay->streamstateindication =
      gst_rtp_mp4g_depay_parse_int (structure, "streamstateindication", 0);
  rtpmp4gdepay->auxiliarydatasizelength =
      gst_rtp_mp4g_depay_parse_int (structure, "auxiliarydatasizelength", 0);

  /* get config string */
  if ((str = gst_structure_get_string (structure, "config"))) {
    GValue v = { 0 };

    g_value_init (&v, GST_TYPE_BUFFER);
    if (gst_value_deserialize (&v, str)) {
      GstBuffer *buffer;

      buffer = gst_value_get_buffer (&v);
      gst_caps_set_simple (srccaps,
          "codec_data", GST_TYPE_BUFFER, buffer, NULL);
      g_value_unset (&v);
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

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    gst_adapter_clear (rtpmp4gdepay->adapter);
  }

  {
    gint payload_len, payload_header;
    guint8 *payload;
    guint32 timestamp;
    guint AU_headers_len;
    guint AU_size, AU_index;
    gboolean M;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);
    payload_header = 0;

    timestamp = gst_rtp_buffer_get_timestamp (buf);
    M = gst_rtp_buffer_get_marker (buf);

    if (rtpmp4gdepay->sizelength > 0) {
      gint num_AU_headers, AU_headers_bytes, i;

      /* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
       * |AU-headers-length|AU-header|AU-header|      |AU-header|padding|
       * |                 |   (1)   |   (2)   |      |   (n) * | bits  |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
       *
       * The length is 2 bytes and contains the length of the following
       * AU-headers in bits.
       */
      AU_headers_len = (payload[0] << 8) | payload[1];
      AU_headers_bytes = (AU_headers_len + 7) / 8;
      num_AU_headers = AU_headers_len / 16;

      GST_DEBUG_OBJECT (rtpmp4gdepay, "AU headers len %d, bytes %d, num %d",
          AU_headers_len, AU_headers_bytes, num_AU_headers);

      /* skip header */
      payload += 2;
      /* skip special headers */
      payload_header = 2 + AU_headers_bytes;

      for (i = 0; i < num_AU_headers; i++) {
        /* FIXME, use bits */
        AU_size = ((payload[0] << 8) | payload[1]) >> 3;
        AU_index = payload[1] & 0x7;
        payload += 2;

        GST_DEBUG_OBJECT (rtpmp4gdepay, "len, %d, size %d, index %d",
            AU_headers_len, AU_size, AU_index);

        /* collect stuff in the adapter, strip header from payload and push in
         * the adapter */
        outbuf =
            gst_rtp_buffer_get_payload_subbuffer (buf, payload_header, AU_size);
        gst_adapter_push (rtpmp4gdepay->adapter, outbuf);

        if (M) {
          guint avail;

          /* packet is complete, flush */
          avail = gst_adapter_available (rtpmp4gdepay->adapter);

          outbuf = gst_adapter_take_buffer (rtpmp4gdepay->adapter, avail);
          gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

          GST_DEBUG ("gst_rtp_mp4g_depay_chain: pushing buffer of size %d",
              GST_BUFFER_SIZE (outbuf));

          /* only apply the timestamp for the first buffer */
          if (i == 0)
            gst_base_rtp_depayload_push_ts (depayload, timestamp, outbuf);
          else
            gst_base_rtp_depayload_push (depayload, outbuf);
        }
        payload_header += AU_size;
      }
    } else {
      /* push complete buffer in adapter */
      outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 0, payload_len);
      gst_adapter_push (rtpmp4gdepay->adapter, outbuf);

      /* if this was the last packet of the VOP, create and push a buffer */
      if (M) {
        guint avail;

        avail = gst_adapter_available (rtpmp4gdepay->adapter);

        outbuf = gst_adapter_take_buffer (rtpmp4gdepay->adapter, avail);
        gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

        GST_DEBUG ("gst_rtp_mp4g_depay_chain: pushing buffer of size %d",
            GST_BUFFER_SIZE (outbuf));

        return outbuf;
      }
    }
  }
  return NULL;

  /* ERRORS */
bad_packet:
  {
    GST_ELEMENT_WARNING (rtpmp4gdepay, STREAM, DECODE,
        ("Packet did not validate."), (NULL));
    return NULL;
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
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpmp4gdepay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
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
