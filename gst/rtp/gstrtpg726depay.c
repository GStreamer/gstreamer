/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) 2005 Zeeshan Ali <zeenix@gmail.com>
 * Copyright (C) 2008 Axis Communications <dev-gstreamer@axis.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpg726depay.h"

GST_DEBUG_CATEGORY_STATIC (rtpg726depay_debug);
#define GST_CAT_DEFAULT (rtpg726depay_debug)

#define DEFAULT_BIT_RATE 32000
#define SAMPLE_RATE 8000
#define LAYOUT_G726 "g726"

/* RtpG726Depay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_FORCE_AAL2	TRUE

enum
{
  PROP_0,
  PROP_FORCE_AAL2,
  PROP_LAST
};

static GstStaticPadTemplate gst_rtp_g726_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "encoding-name = (string) { \"G726\", \"G726-16\", \"G726-24\", \"G726-32\", \"G726-40\", "
        "\"AAL2-G726-16\", \"AAL2-G726-24\", \"AAL2-G726-32\", \"AAL2-G726-40\" }, "
        "clock-rate = (int) 8000;")
    );

static GstStaticPadTemplate gst_rtp_g726_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-adpcm, "
        "channels = (int) 1, "
        "rate = (int) 8000, "
        "bitrate = (int) { 16000, 24000, 32000, 40000 }, "
        "layout = (string) \"g726\"")
    );

static void gst_rtp_g726_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_g726_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstBuffer *gst_rtp_g726_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_g726_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpG726Depay, gst_rtp_g726_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtp_g726_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_g726_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_g726_depay_sink_template);
  gst_element_class_set_details_simple (element_class, "RTP G.726 depayloader",
      "Codec/Depayloader/Network/RTP",
      "Extracts G.726 audio from RTP packets",
      "Axis Communications <dev-gstreamer@axis.com>");
}

static void
gst_rtp_g726_depay_class_init (GstRtpG726DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->set_property = gst_rtp_g726_depay_set_property;
  gobject_class->get_property = gst_rtp_g726_depay_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FORCE_AAL2,
      g_param_spec_boolean ("force-aal2", "Force AAL2",
          "Force AAL2 decoding for compatibility with bad payloaders",
          DEFAULT_FORCE_AAL2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasertpdepayload_class->process = gst_rtp_g726_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_g726_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpg726depay_debug, "rtpg726depay", 0,
      "G.726 RTP Depayloader");
}

static void
gst_rtp_g726_depay_init (GstRtpG726Depay * rtpG726depay,
    GstRtpG726DepayClass * klass)
{
  GstBaseRTPDepayload *depayload;

  depayload = GST_BASE_RTP_DEPAYLOAD (rtpG726depay);

  gst_pad_use_fixed_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload));

  rtpG726depay->force_aal2 = DEFAULT_FORCE_AAL2;
}

static gboolean
gst_rtp_g726_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure;
  gboolean ret;
  gint clock_rate;
  const gchar *encoding_name;
  GstRtpG726Depay *depay;

  depay = GST_RTP_G726_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 8000;          /* default */
  depayload->clock_rate = clock_rate;

  depay->aal2 = FALSE;
  encoding_name = gst_structure_get_string (structure, "encoding-name");
  if (encoding_name == NULL || g_ascii_strcasecmp (encoding_name, "G726") == 0) {
    depay->bitrate = DEFAULT_BIT_RATE;
  } else {
    if (g_str_has_prefix (encoding_name, "AAL2-")) {
      depay->aal2 = TRUE;
      encoding_name += 5;
    }
    if (g_ascii_strcasecmp (encoding_name, "G726-16") == 0) {
      depay->bitrate = 16000;
    } else if (g_ascii_strcasecmp (encoding_name, "G726-24") == 0) {
      depay->bitrate = 24000;
    } else if (g_ascii_strcasecmp (encoding_name, "G726-32") == 0) {
      depay->bitrate = 32000;
    } else if (g_ascii_strcasecmp (encoding_name, "G726-40") == 0) {
      depay->bitrate = 40000;
    } else
      goto unknown_encoding;
  }

  GST_DEBUG ("RTP G.726 depayloader, bitrate set to %d\n", depay->bitrate);

  srccaps = gst_caps_new_simple ("audio/x-adpcm",
      "channels", G_TYPE_INT, 1,
      "rate", G_TYPE_INT, clock_rate,
      "bitrate", G_TYPE_INT, depay->bitrate,
      "layout", G_TYPE_STRING, LAYOUT_G726, NULL);

  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return ret;

  /* ERRORS */
unknown_encoding:
  {
    GST_WARNING ("Could not determine bitrate from encoding-name (%s)",
        encoding_name);
    return FALSE;
  }
}


static GstBuffer *
gst_rtp_g726_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpG726Depay *depay;
  GstBuffer *outbuf = NULL;
  gboolean marker;

  depay = GST_RTP_G726_DEPAY (depayload);

  marker = gst_rtp_buffer_get_marker (buf);

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf), marker,
      gst_rtp_buffer_get_timestamp (buf), gst_rtp_buffer_get_seq (buf));

  if (depay->aal2 || depay->force_aal2) {
    /* AAL2, we can just copy the bytes */
    outbuf = gst_rtp_buffer_get_payload_buffer (buf);
    if (!outbuf)
      goto bad_len;
  } else {
    guint8 *in, *out, tmp;
    guint len;

    in = gst_rtp_buffer_get_payload (buf);
    len = gst_rtp_buffer_get_payload_len (buf);

    if (gst_buffer_is_writable (buf)) {
      outbuf = gst_rtp_buffer_get_payload_buffer (buf);
    } else {
      GstBuffer *copy;

      /* copy buffer */
      copy = gst_buffer_copy (buf);
      outbuf = gst_rtp_buffer_get_payload_buffer (copy);
      gst_buffer_unref (copy);
    }

    if (!outbuf)
      goto bad_len;

    out = GST_BUFFER_DATA (outbuf);

    /* we need to reshuffle the bytes, input is always of the form
     * A B C D ... with the number of bits depending on the bitrate. */
    switch (depay->bitrate) {
      case 16000:
      {
        /*  0               
         *  0 1 2 3 4 5 6 7 
         * +-+-+-+-+-+-+-+-+-
         * |D D|C C|B B|A A| ...
         * |0 1|0 1|0 1|0 1|
         * +-+-+-+-+-+-+-+-+-
         */
        while (len > 0) {
          tmp = *in++;
          *out++ = ((tmp & 0xc0) >> 6) |
              ((tmp & 0x30) >> 2) | ((tmp & 0x0c) << 2) | ((tmp & 0x03) << 6);
          len--;
        }
        break;
      }
      case 24000:
      {
        /*  0                   1                   2
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
         * |C C|B B B|A A A|F|E E E|D D D|C|H H H|G G G|F F| ...
         * |1 2|0 1 2|0 1 2|2|0 1 2|0 1 2|0|0 1 2|0 1 2|0 1|
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
         */
        while (len > 2) {
          tmp = *in++;
          *out++ = ((tmp & 0xe0) >> 5) |
              ((tmp & 0x1c) << 1) | ((tmp & 0x03) << 6);
          tmp = *in++;
          *out++ = ((tmp & 0x80) >> 7) |
              ((tmp & 0x70) >> 3) | ((tmp & 0x0e) << 4) | ((tmp & 0x01) << 7);
          tmp = *in++;
          *out++ = ((tmp & 0xc0) >> 6) |
              ((tmp & 0x38) >> 1) | ((tmp & 0x07) << 5);
          len -= 3;
        }
        break;
      }
      case 32000:
      {
        /*  0                   1
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
         * |B B B B|A A A A|D D D D|C C C C| ...
         * |0 1 2 3|0 1 2 3|0 1 2 3|0 1 2 3|
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
         */
        while (len > 0) {
          tmp = *in++;
          *out++ = ((tmp & 0xf0) >> 4) | ((tmp & 0x0f) << 4);
          len--;
        }
        break;
      }
      case 40000:
      {
        /*  0                   1                   2                   3                   4
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
         * |B B B|A A A A A|D|C C C C C|B B|E E E E|D D D D|G G|F F F F F|E|H H H H H|G G G|
         * |2 3 4|0 1 2 3 4|4|0 1 2 3 4|0 1|1 2 3 4|0 1 2 3|3 4|0 1 2 3 4|0|0 1 2 3 4|0 1 2|   
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
         */
        while (len > 4) {
          tmp = *in++;
          *out++ = ((tmp & 0xf8) >> 3) | ((tmp & 0x07) << 5);
          tmp = *in++;
          *out++ = ((tmp & 0xc0) >> 6) |
              ((tmp & 0x3e) << 1) | ((tmp & 0x01) << 7);
          tmp = *in++;
          *out++ = ((tmp & 0xf0) >> 4) | ((tmp & 0x0f) << 4);
          tmp = *in++;
          *out++ = ((tmp & 0x80) >> 7) |
              ((tmp & 0x7c) >> 1) | ((tmp & 0x03) << 6);
          tmp = *in++;
          *out++ = ((tmp & 0xe0) >> 5) | ((tmp & 0x1f) << 3);
          len -= 5;
        }
        break;
      }
    }
  }

  if (marker) {
    /* mark start of talkspurt with discont */
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  }

  return outbuf;

bad_len:
  return NULL;
}

static void
gst_rtp_g726_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpG726Depay *rtpg726depay;

  rtpg726depay = GST_RTP_G726_DEPAY (object);

  switch (prop_id) {
    case PROP_FORCE_AAL2:
      rtpg726depay->force_aal2 = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_g726_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpG726Depay *rtpg726depay;

  rtpg726depay = GST_RTP_G726_DEPAY (object);

  switch (prop_id) {
    case PROP_FORCE_AAL2:
      g_value_set_boolean (value, rtpg726depay->force_aal2);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_g726_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg726depay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_G726_DEPAY);
}
