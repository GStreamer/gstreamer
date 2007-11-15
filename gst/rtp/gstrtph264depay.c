/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtph264depay.h"

GST_DEBUG_CATEGORY_STATIC (rtph264depay_debug);
#define GST_CAT_DEFAULT (rtph264depay_debug)

/* 3 zero bytes syncword */
static const guint8 sync_bytes[] = { 0, 0, 0, 1 };

/* elementfactory information */
static const GstElementDetails gst_rtp_h264depay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts H264 video from RTP packets (RFC 3984)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpH264Depay signals and args */
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

static GstStaticPadTemplate gst_rtp_h264_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264")
    );

static GstStaticPadTemplate gst_rtp_h264_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H264\"")
        /** optional parameters **/
    /* "profile-level-id = (string) ANY, " */
    /* "max-mbps = (string) ANY, " */
    /* "max-fs = (string) ANY, " */
    /* "max-cpb = (string) ANY, " */
    /* "max-dpb = (string) ANY, " */
    /* "max-br = (string) ANY, " */
    /* "redundant-pic-cap = (string) { \"0\", \"1\" }, " */
    /* "sprop-parameter-sets = (string) ANY, " */
    /* "parameter-add = (string) { \"0\", \"1\" }, " */
    /* "packetization-mode = (string) { \"0\", \"1\", \"2\" }, " */
    /* "sprop-interleaving-depth = (string) ANY, " */
    /* "sprop-deint-buf-req = (string) ANY, " */
    /* "deint-buf-cap = (string) ANY, " */
    /* "sprop-init-buf-time = (string) ANY, " */
    /* "sprop-max-don-diff = (string) ANY, " */
    /* "max-rcmd-nalu-size = (string) ANY " */
    );

GST_BOILERPLATE (GstRtpH264Depay, gst_rtp_h264_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_h264_depay_finalize (GObject * object);
static void gst_rtp_h264_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h264_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_h264_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstBuffer *gst_rtp_h264_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_h264_depay_setcaps (GstBaseRTPDepayload * filter,
    GstCaps * caps);

static void
gst_rtp_h264_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h264_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h264_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h264depay_details);
}

static void
gst_rtp_h264_depay_class_init (GstRtpH264DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_h264_depay_finalize;
  gobject_class->set_property = gst_rtp_h264_depay_set_property;
  gobject_class->get_property = gst_rtp_h264_depay_get_property;

  gstelement_class->change_state = gst_rtp_h264_depay_change_state;

  gstbasertpdepayload_class->process = gst_rtp_h264_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_h264_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtph264depay_debug, "rtph264depay", 0,
      "H264 Video RTP Depayloader");
}

static void
gst_rtp_h264_depay_init (GstRtpH264Depay * rtph264depay,
    GstRtpH264DepayClass * klass)
{
  rtph264depay->adapter = gst_adapter_new ();
}

static void
gst_rtp_h264_depay_finalize (GObject * object)
{
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (object);

  if (rtph264depay->codec_data)
    gst_buffer_unref (rtph264depay->codec_data);

  g_object_unref (rtph264depay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const guint8 a2bin[256] = {
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
  64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
  64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static guint
decode_base64 (gchar * in, guint8 * out)
{
  guint8 v1, v2;
  guint len = 0;

  v1 = a2bin[(gint) * in];
  while (v1 <= 63) {
    /* read 4 bytes, write 3 bytes, invalid base64 are zeroes */
    v2 = a2bin[(gint) * ++in];
    *out++ = (v1 << 2) | ((v2 & 0x3f) >> 4);
    v1 = (v2 > 63 ? 64 : a2bin[(gint) * ++in]);
    *out++ = (v2 << 4) | ((v1 & 0x3f) >> 2);
    v2 = (v1 > 63 ? 64 : a2bin[(gint) * ++in]);
    *out++ = (v1 << 6) | (v2 & 0x3f);
    v1 = (v2 > 63 ? 64 : a2bin[(gint) * ++in]);
    len += 3;
  }
  /* move to '\0' */
  while (*in != '\0')
    in++;

  /* subtract padding */
  while (len > 0 && *--in == '=')
    len--;

  return len;
}

static gboolean
gst_rtp_h264_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps = NULL;
  gint clock_rate = 90000;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (depayload);

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("video/x-h264", NULL);

  if (gst_structure_has_field (structure, "sprop-parameter-sets")) {
    const gchar *ps;
    gchar **params;
    guint len, total;
    gint i;
    GstBuffer *codec_data;
    guint8 *b64;

    /* Base64 encoded, comma separated config NALs */
    ps = gst_structure_get_string (structure, "sprop-parameter-sets");
    params = g_strsplit (ps, ",", 0);

    /* count total number of bytes in base64. Also include the sync bytes in
     * front of the params. */
    len = 0;
    for (i = 0; params[i]; i++) {
      len += strlen (params[i]);
      len += sizeof (sync_bytes);
    }
    /* we seriously overshoot the length, but it's fine. */
    codec_data = gst_buffer_new_and_alloc (len);
    b64 = GST_BUFFER_DATA (codec_data);
    total = 0;
    for (i = 0; params[i]; i++) {
      GST_DEBUG_OBJECT (depayload, "decoding param %d", i);
      memcpy (b64, sync_bytes, sizeof (sync_bytes));
      b64 += sizeof (sync_bytes);
      len = decode_base64 (params[i], b64);
      total += len + sizeof (sync_bytes);
      b64 += len;
    }
    GST_BUFFER_SIZE (codec_data) = total;
    g_strfreev (params);

    /* keep the codec_data, we need to send it as the first buffer. We cannot
     * push it in the adapter because the adapter might be flushed on discont.
     */
    if (rtph264depay->codec_data)
      gst_buffer_unref (rtph264depay->codec_data);
    rtph264depay->codec_data = codec_data;
  }

  gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

static GstBuffer *
gst_rtp_h264_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpH264Depay *rtph264depay;
  GstBuffer *outbuf;
  guint8 nal_unit_type;

  rtph264depay = GST_RTP_H264_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    gst_adapter_clear (rtph264depay->adapter);
    rtph264depay->wait_start = TRUE;
  }

  {
    gint payload_len;
    guint8 *payload;
    guint header_len;
    guint8 nal_ref_idc;
    guint8 *outdata;
    guint outsize, nalu_size;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    GST_DEBUG_OBJECT (rtph264depay, "receiving %d bytes", payload_len);

    /* +---------------+
     * |0|1|2|3|4|5|6|7|
     * +-+-+-+-+-+-+-+-+
     * |F|NRI|  Type   |
     * +---------------+
     *
     * F must be 0.
     */
    nal_ref_idc = (payload[0] & 0x60) >> 5;
    nal_unit_type = payload[0] & 0x1f;

    /* at least one byte header with type */
    header_len = 1;

    GST_DEBUG_OBJECT (rtph264depay, "NRI %d, Type %d", nal_ref_idc,
        nal_unit_type);

    switch (nal_unit_type) {
      case 0:
      case 30:
      case 31:
        /* undefined */
        goto undefined_type;
      case 25:
        /* STAP-B    Single-time aggregation packet     5.7.1 */
        /* 2 byte extra header for DON */
        header_len += 2;
        /* fallthrough */
      case 24:
      {
        /* strip headers */
        payload += header_len;
        payload_len -= header_len;

        rtph264depay->wait_start = FALSE;

        /* prepend codec_data */
        if (rtph264depay->codec_data) {
          gst_adapter_push (rtph264depay->adapter, rtph264depay->codec_data);
          rtph264depay->codec_data = NULL;
        }

        /* STAP-A    Single-time aggregation packet     5.7.1 */
        while (payload_len > 2) {
          /*                      1          
           *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           * |         NALU Size             |
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           */
          nalu_size = (payload[0] << 8) | payload[1];

          /* strip NALU size */
          payload += 2;
          payload_len -= 2;

          if (nalu_size > payload_len)
            nalu_size = payload_len;

          outsize = nalu_size + sizeof (sync_bytes);
          outbuf = gst_buffer_new_and_alloc (outsize);
          outdata = GST_BUFFER_DATA (outbuf);
          memcpy (outdata, sync_bytes, sizeof (sync_bytes));
          outdata += sizeof (sync_bytes);
          memcpy (outdata, payload, nalu_size);

          gst_adapter_push (rtph264depay->adapter, outbuf);

          payload += nalu_size;
          payload_len -= nalu_size;
        }

        outsize = gst_adapter_available (rtph264depay->adapter);
        outbuf = gst_adapter_take_buffer (rtph264depay->adapter, outsize);

        gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

        return outbuf;
      }
      case 26:
        /* MTAP16    Multi-time aggregation packet      5.7.2 */
        header_len = 5;
      case 27:
        /* MTAP24    Multi-time aggregation packet      5.7.2 */
        header_len = 6;
        goto not_implemented;
        break;
      case 28:
      case 29:
      {
        /* FU-A      Fragmentation unit                 5.8 */
        /* FU-B      Fragmentation unit                 5.8 */
        gboolean S, E;

        /* +---------------+
         * |0|1|2|3|4|5|6|7|
         * +-+-+-+-+-+-+-+-+
         * |S|E|R|  Type   |
         * +---------------+
         *
         * R is reserved and always 0
         */
        S = (payload[1] & 0x80) == 0x80;
        E = (payload[1] & 0x40) == 0x40;

        GST_DEBUG_OBJECT (rtph264depay, "S %d, E %d", S, E);

        if (rtph264depay->wait_start && !S)
          goto waiting_start;

        if (S) {
          /* NAL unit starts here */
          guint8 nal_header;

          rtph264depay->wait_start = FALSE;

          /* reconstruct NAL header */
          nal_header = (payload[0] & 0xe0) | (payload[1] & 0x1f);

          /* strip type header, keep FU header, we'll reuse it to reconstruct
           * the NAL header. */
          payload += 1;
          payload_len -= 1;

          nalu_size = payload_len;
          outsize = nalu_size + sizeof (sync_bytes);
          outbuf = gst_buffer_new_and_alloc (outsize);
          outdata = GST_BUFFER_DATA (outbuf);
          memcpy (outdata, sync_bytes, sizeof (sync_bytes));
          outdata += sizeof (sync_bytes);
          memcpy (outdata, payload, nalu_size);
          outdata[0] = nal_header;

          GST_DEBUG_OBJECT (rtph264depay, "queueing %d bytes", outsize);

          /* and assemble in the adapter */
          gst_adapter_push (rtph264depay->adapter, outbuf);
        } else {
          /* strip off FU indicator and FU header bytes */
          payload += 2;
          payload_len -= 2;

          outsize = payload_len;
          outbuf = gst_buffer_new_and_alloc (outsize);
          outdata = GST_BUFFER_DATA (outbuf);
          memcpy (outdata, payload, outsize);

          GST_DEBUG_OBJECT (rtph264depay, "queueing %d bytes", outsize);

          /* and assemble in the adapter */
          gst_adapter_push (rtph264depay->adapter, outbuf);
        }

        /* if NAL unit ends, flush the adapter */
        if (E) {
          GST_DEBUG_OBJECT (rtph264depay, "output %d bytes", outsize);

          outsize = gst_adapter_available (rtph264depay->adapter);
          outbuf = gst_adapter_take_buffer (rtph264depay->adapter, outsize);

          gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

          /* push codec_data first */
          if (rtph264depay->codec_data) {
            gst_buffer_set_caps (rtph264depay->codec_data,
                GST_PAD_CAPS (depayload->srcpad));
            gst_base_rtp_depayload_push (depayload, rtph264depay->codec_data);
            rtph264depay->codec_data = NULL;
          }
          return outbuf;
        }
        break;
      }
      default:
      {
        rtph264depay->wait_start = FALSE;

        /* 1-23   NAL unit  Single NAL unit packet per H.264   5.6 */
        /* the entire payload is the output buffer */
        nalu_size = payload_len;
        outsize = nalu_size + sizeof (sync_bytes);
        outbuf = gst_buffer_new_and_alloc (outsize);
        outdata = GST_BUFFER_DATA (outbuf);
        memcpy (outdata, sync_bytes, sizeof (sync_bytes));
        outdata += sizeof (sync_bytes);
        memcpy (outdata, payload, nalu_size);

        gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

        /* push codec_data first */
        if (rtph264depay->codec_data) {
          gst_buffer_set_caps (rtph264depay->codec_data,
              GST_PAD_CAPS (depayload->srcpad));
          gst_base_rtp_depayload_push (depayload, rtph264depay->codec_data);
          rtph264depay->codec_data = NULL;
        }
        return outbuf;
      }
    }
  }

  return NULL;

  /* ERRORS */
bad_packet:
  {
    GST_ELEMENT_WARNING (rtph264depay, STREAM, DECODE,
        (NULL), ("Packet did not validate"));
    return NULL;
  }
undefined_type:
  {
    GST_ELEMENT_WARNING (rtph264depay, STREAM, DECODE,
        (NULL), ("Undefined packet type"));
    return NULL;
  }
waiting_start:
  {
    GST_DEBUG_OBJECT (rtph264depay, "waiting for start");
    return NULL;
  }
not_implemented:
  {
    GST_ELEMENT_ERROR (rtph264depay, STREAM, FORMAT,
        (NULL), ("NAL unit type %d not supported yet", nal_unit_type));
    return NULL;
  }
}

static void
gst_rtp_h264_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h264_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_h264_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpH264Depay *rtph264depay;
  GstStateChangeReturn ret;

  rtph264depay = GST_RTP_H264_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtph264depay->adapter);
      rtph264depay->wait_start = TRUE;
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
gst_rtp_h264_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph264depay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_H264_DEPAY);
}
