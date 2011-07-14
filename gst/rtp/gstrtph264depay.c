/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
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
#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtph264depay.h"

GST_DEBUG_CATEGORY_STATIC (rtph264depay_debug);
#define GST_CAT_DEFAULT (rtph264depay_debug)

#define DEFAULT_BYTE_STREAM	TRUE
#define DEFAULT_ACCESS_UNIT	FALSE

enum
{
  PROP_0,
  PROP_BYTE_STREAM,
  PROP_ACCESS_UNIT,
  PROP_LAST
};


/* 3 zero bytes syncword */
static const guint8 sync_bytes[] = { 0, 0, 0, 1 };

static GstStaticPadTemplate gst_rtp_h264_depay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) avc, alignment = (string) au; "
        "video/x-h264, "
        "stream-format = (string) byte-stream, alignment = (string) { nal, au }")
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
static gboolean gst_rtp_h264_depay_handle_event (GstBaseRTPDepayload * depay,
    GstEvent * event);

static void
gst_rtp_h264_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_h264_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_h264_depay_sink_template);

  gst_element_class_set_details_simple (element_class, "RTP H264 depayloader",
      "Codec/Depayloader/Network/RTP",
      "Extracts H264 video from RTP packets (RFC 3984)",
      "Wim Taymans <wim.taymans@gmail.com>");
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

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BYTE_STREAM,
      g_param_spec_boolean ("byte-stream", "Byte Stream",
          "Generate byte stream format of NALU (deprecated; use caps)",
          DEFAULT_BYTE_STREAM, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ACCESS_UNIT,
      g_param_spec_boolean ("access-unit", "Access Unit",
          "Merge NALU into AU (picture) (deprecated; use caps)",
          DEFAULT_ACCESS_UNIT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_rtp_h264_depay_change_state;

  gstbasertpdepayload_class->process = gst_rtp_h264_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_h264_depay_setcaps;
  gstbasertpdepayload_class->handle_event = gst_rtp_h264_depay_handle_event;

  GST_DEBUG_CATEGORY_INIT (rtph264depay_debug, "rtph264depay", 0,
      "H264 Video RTP Depayloader");
}

static void
gst_rtp_h264_depay_init (GstRtpH264Depay * rtph264depay,
    GstRtpH264DepayClass * klass)
{
  rtph264depay->adapter = gst_adapter_new ();
  rtph264depay->picture_adapter = gst_adapter_new ();
  rtph264depay->byte_stream = DEFAULT_BYTE_STREAM;
  rtph264depay->merge = DEFAULT_ACCESS_UNIT;
  rtph264depay->sps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_buffer_unref);
  rtph264depay->pps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_buffer_unref);
}

static void
gst_rtp_h264_depay_reset (GstRtpH264Depay * rtph264depay)
{
  gst_adapter_clear (rtph264depay->adapter);
  rtph264depay->wait_start = TRUE;
  gst_adapter_clear (rtph264depay->picture_adapter);
  rtph264depay->picture_start = FALSE;
  rtph264depay->last_keyframe = FALSE;
  rtph264depay->last_ts = 0;
  rtph264depay->current_fu_type = 0;
  rtph264depay->new_codec_data = FALSE;
  g_ptr_array_set_size (rtph264depay->sps, 0);
  g_ptr_array_set_size (rtph264depay->pps, 0);
}

static void
gst_rtp_h264_depay_finalize (GObject * object)
{
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (object);

  if (rtph264depay->codec_data)
    gst_buffer_unref (rtph264depay->codec_data);

  g_object_unref (rtph264depay->adapter);
  g_object_unref (rtph264depay->picture_adapter);

  g_ptr_array_free (rtph264depay->sps, TRUE);
  g_ptr_array_free (rtph264depay->pps, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_h264_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (object);

  switch (prop_id) {
    case PROP_BYTE_STREAM:
      rtph264depay->byte_stream = g_value_get_boolean (value);
      break;
    case PROP_ACCESS_UNIT:
      rtph264depay->merge = g_value_get_boolean (value);
      break;
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
    case PROP_BYTE_STREAM:
      g_value_set_boolean (value, rtph264depay->byte_stream);
      break;
    case PROP_ACCESS_UNIT:
      g_value_set_boolean (value, rtph264depay->merge);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h264_depay_negotiate (GstRtpH264Depay * rtph264depay)
{
  GstCaps *caps;
  gint byte_stream = -1;
  gint merge = -1;

  caps =
      gst_pad_get_allowed_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (rtph264depay));

  GST_DEBUG_OBJECT (rtph264depay, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (caps) {
    if (gst_caps_get_size (caps) > 0) {
      GstStructure *s = gst_caps_get_structure (caps, 0);
      const gchar *str = NULL;

      if ((str = gst_structure_get_string (s, "stream-format"))) {
        if (strcmp (str, "avc") == 0) {
          byte_stream = FALSE;
        } else if (strcmp (str, "byte-stream") == 0) {
          byte_stream = TRUE;
        } else {
          GST_DEBUG_OBJECT (rtph264depay, "unknown stream-format: %s", str);
        }
      }

      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0) {
          merge = TRUE;
        } else if (strcmp (str, "nal") == 0) {
          merge = FALSE;
        } else {
          GST_DEBUG_OBJECT (rtph264depay, "unknown alignment: %s", str);
        }
      }
    }
    gst_caps_unref (caps);
  }

  if (byte_stream >= 0) {
    GST_DEBUG_OBJECT (rtph264depay, "downstream requires byte-stream %d",
        byte_stream);
    if (rtph264depay->byte_stream != byte_stream) {
      GST_WARNING_OBJECT (rtph264depay,
          "overriding property setting based on caps");
      rtph264depay->byte_stream = byte_stream;
    }
  }
  if (merge >= 0) {
    GST_DEBUG_OBJECT (rtph264depay, "downstream requires merge %d", merge);
    if (rtph264depay->merge != merge) {
      GST_WARNING_OBJECT (rtph264depay,
          "overriding property setting based on caps");
      rtph264depay->merge = merge;
    }
  }
}

/* Stolen from bad/gst/mpegtsdemux/payloader_parsers.c */
/* variable length Exp-Golomb parsing according to H.264 spec 9.1*/
static gboolean
read_golomb (GstBitReader * br, guint32 * value)
{
  guint8 b, leading_zeros = -1;
  *value = 1;

  for (b = 0; !b; leading_zeros++) {
    if (!gst_bit_reader_get_bits_uint8 (br, &b, 1))
      return FALSE;
    *value *= 2;
  }

  *value = (*value >> 1) - 1;
  if (leading_zeros > 0) {
    guint32 tmp = 0;
    if (!gst_bit_reader_get_bits_uint32 (br, &tmp, leading_zeros))
      return FALSE;
    *value += tmp;
  }

  return TRUE;
}

static gboolean
parse_sps (GstBuffer * nal, guint32 * sps_id)
{
  GstBitReader br = GST_BIT_READER_INIT (GST_BUFFER_DATA (nal) + 4,
      GST_BUFFER_SIZE (nal) - 4);

  if (GST_BUFFER_SIZE (nal) < 5)
    return FALSE;

  if (!read_golomb (&br, sps_id))
    return FALSE;

  return TRUE;
}

static gboolean
parse_pps (GstBuffer * nal, guint32 * sps_id, guint32 * pps_id)
{
  GstBitReader br = GST_BIT_READER_INIT (GST_BUFFER_DATA (nal) + 1,
      GST_BUFFER_SIZE (nal) - 1);

  if (GST_BUFFER_SIZE (nal) < 2)
    return FALSE;

  if (!read_golomb (&br, pps_id))
    return FALSE;
  if (!read_golomb (&br, sps_id))
    return FALSE;

  return TRUE;
}

static gboolean
gst_rtp_h264_set_src_caps (GstRtpH264Depay * rtph264depay)
{
  gboolean res;
  GstCaps *srccaps;
  guchar level = 0;
  guchar profile_compat = G_MAXUINT8;

  if (!rtph264depay->byte_stream &&
      (!rtph264depay->new_codec_data ||
          rtph264depay->sps->len == 0 || rtph264depay->pps->len == 0))
    return TRUE;

  srccaps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING,
      rtph264depay->byte_stream ? "byte-stream" : "avc",
      "alignment", G_TYPE_STRING, rtph264depay->merge ? "au" : "nal", NULL);

  if (!rtph264depay->byte_stream) {
    GstBuffer *codec_data;
    guchar *data;
    guint len;
    guint i;

    /* start with 7 bytes header */
    len = 7;
    /* count sps & pps */
    for (i = 0; i < rtph264depay->sps->len; i++)
      len += 2 + GST_BUFFER_SIZE (g_ptr_array_index (rtph264depay->sps, i));
    for (i = 0; i < rtph264depay->pps->len; i++)
      len += 2 + GST_BUFFER_SIZE (g_ptr_array_index (rtph264depay->pps, i));

    codec_data = gst_buffer_new_and_alloc (len);
    g_debug ("alloc_len: %u", len);
    data = GST_BUFFER_DATA (codec_data);

    /* 8 bits version == 1 */
    *data++ = 1;

    /* According to: ISO/IEC 14496-15:2004(E) section 5.2.4.1
     * The level is the max level of all SPSes
     * A profile compat bit can only be set if all SPSes include that bit
     */
    for (i = 0; i < rtph264depay->sps->len; i++) {
      guchar *nal = GST_BUFFER_DATA (g_ptr_array_index (rtph264depay->sps, i));

      profile_compat &= nal[2];
      level = MAX (level, nal[3]);
    }

    /* Assume all SPSes use the same profile, so extract from the first SPS */
    *data++ = GST_BUFFER_DATA (g_ptr_array_index (rtph264depay->sps, 0))[1];
    *data++ = profile_compat;
    *data++ = level;

    /* 6 bits reserved | 2 bits lengthSizeMinusOn */
    *data++ = 0xff;
    /* 3 bits reserved | 5 bits numOfSequenceParameterSets */
    *data++ = 0xe0 | (rtph264depay->sps->len & 0x1f);

    /* copy all SPS */
    for (i = 0; i < rtph264depay->sps->len; i++) {
      GstBuffer *sps = g_ptr_array_index (rtph264depay->sps, i);

      GST_DEBUG_OBJECT (rtph264depay, "copy SPS %d of length %d", i,
          GST_BUFFER_SIZE (sps));
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (sps));
      data += 2;
      memcpy (data, GST_BUFFER_DATA (sps), GST_BUFFER_SIZE (sps));
      data += GST_BUFFER_SIZE (sps);
    }

    /* 8 bits numOfPictureParameterSets */
    *data++ = rtph264depay->pps->len;
    /* copy all PPS */
    for (i = 0; i < rtph264depay->pps->len; i++) {
      GstBuffer *pps = g_ptr_array_index (rtph264depay->pps, i);

      GST_DEBUG_OBJECT (rtph264depay, "copy PPS %d of length %d", i,
          GST_BUFFER_SIZE (pps));
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (pps));
      data += 2;
      memcpy (data, GST_BUFFER_DATA (pps), GST_BUFFER_SIZE (pps));
      data += GST_BUFFER_SIZE (pps);
    }

    GST_BUFFER_SIZE (codec_data) = data - GST_BUFFER_DATA (codec_data);

    gst_caps_set_simple (srccaps,
        "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    gst_buffer_unref (codec_data);
  }

  res = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (rtph264depay),
      srccaps);
  gst_caps_unref (srccaps);

  if (res)
    rtph264depay->new_codec_data = FALSE;

  return res;
}

static gboolean
gst_rtp_h264_add_sps_pps (GstRtpH264Depay * rtph264depay, GstBuffer * nal)
{
  guchar type = GST_BUFFER_DATA (nal)[0] & 0x1f;
  guint i;

  if (type == 7) {
    guint32 sps_id;

    if (!parse_sps (nal, &sps_id)) {
      GST_WARNING_OBJECT (rtph264depay, "Invalid SPS,"
          " can't parse seq_parameter_set_id");
      goto drop;
    }

    for (i = 0; i < rtph264depay->sps->len; i++) {
      GstBuffer *sps = g_ptr_array_index (rtph264depay->sps, i);
      guint32 tmp_sps_id;

      parse_sps (sps, &tmp_sps_id);
      if (sps_id == tmp_sps_id) {
        if (GST_BUFFER_SIZE (nal) == GST_BUFFER_SIZE (sps) &&
            memcmp (GST_BUFFER_DATA (nal), GST_BUFFER_DATA (sps),
                GST_BUFFER_SIZE (sps)) == 0) {
          GST_LOG_OBJECT (rtph264depay, "Unchanged SPS %u, not updating",
              sps_id);
          goto drop;
        } else {
          g_ptr_array_remove_index_fast (rtph264depay->sps, i);
          g_ptr_array_add (rtph264depay->sps, nal);
          GST_LOG_OBJECT (rtph264depay, "Modified SPS %u, replacing", sps_id);
          goto done;
        }
      }
    }
    GST_LOG_OBJECT (rtph264depay, "Adding new SPS %u", sps_id);
    g_ptr_array_add (rtph264depay->sps, nal);
  } else if (type == 8) {
    guint32 sps_id;
    guint32 pps_id;

    if (!parse_pps (nal, &sps_id, &pps_id)) {
      GST_WARNING_OBJECT (rtph264depay, "Invalid PPS,"
          " can't parse seq_parameter_set_id or pic_parameter_set_id");
      goto drop;
    }

    for (i = 0; i < rtph264depay->pps->len; i++) {
      GstBuffer *pps = g_ptr_array_index (rtph264depay->pps, i);
      guint32 tmp_sps_id;
      guint32 tmp_pps_id;

      parse_pps (pps, &tmp_sps_id, &tmp_pps_id);
      if (sps_id == tmp_sps_id && pps_id == tmp_pps_id) {
        if (GST_BUFFER_SIZE (nal) == GST_BUFFER_SIZE (pps) &&
            memcmp (GST_BUFFER_DATA (nal), GST_BUFFER_DATA (pps),
                GST_BUFFER_SIZE (pps)) == 0) {
          GST_LOG_OBJECT (rtph264depay, "Unchanged PPS %u:%u, not updating",
              sps_id, pps_id);
          goto drop;
        } else {
          g_ptr_array_remove_index_fast (rtph264depay->pps, i);
          g_ptr_array_add (rtph264depay->pps, nal);
          GST_LOG_OBJECT (rtph264depay, "Modified PPS %u:%u, replacing",
              sps_id, pps_id);
          goto done;
        }
      }
    }
    GST_LOG_OBJECT (rtph264depay, "Adding new PPS %u:%i", sps_id, pps_id);
    g_ptr_array_add (rtph264depay->pps, nal);
  } else {
    goto drop;
  }

done:
  rtph264depay->new_codec_data = TRUE;

  return TRUE;

drop:
  gst_buffer_unref (nal);

  return FALSE;
}

static gboolean
gst_rtp_h264_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  gint clock_rate;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstRtpH264Depay *rtph264depay;
  const gchar *ps;
  GstBuffer *codec_data;
  guint8 *b64;

  rtph264depay = GST_RTP_H264_DEPAY (depayload);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;
  depayload->clock_rate = clock_rate;

  /* Base64 encoded, comma separated config NALs */
  ps = gst_structure_get_string (structure, "sprop-parameter-sets");

  /* negotiate with downstream w.r.t. output format and alignment */
  gst_rtp_h264_depay_negotiate (rtph264depay);

  if (rtph264depay->byte_stream && ps != NULL) {
    /* for bytestream we only need the parameter sets but we don't error out
     * when they are not there, we assume they are in the stream. */
    gchar **params;
    guint len, total;
    gint i;

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
      guint save = 0;
      gint state = 0;

      GST_DEBUG_OBJECT (depayload, "decoding param %d (%s)", i, params[i]);
      memcpy (b64, sync_bytes, sizeof (sync_bytes));
      b64 += sizeof (sync_bytes);
      len =
          g_base64_decode_step (params[i], strlen (params[i]), b64, &state,
          &save);
      GST_DEBUG_OBJECT (depayload, "decoded %d bytes", len);
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
  } else if (!rtph264depay->byte_stream) {
    gchar **params;
    gint i;

    if (ps == NULL)
      goto incomplete_caps;

    params = g_strsplit (ps, ",", 0);

    GST_DEBUG_OBJECT (depayload, "we have %d params", g_strv_length (params));

    /* start with 7 bytes header */
    for (i = 0; params[i]; i++) {
      GstBuffer *nal;
      gsize nal_len;
      guint save = 0;
      gint state = 0;

      nal_len = strlen (params[i]);
      nal = gst_buffer_new_and_alloc (nal_len);

      nal_len =
          g_base64_decode_step (params[i], nal_len, GST_BUFFER_DATA (nal),
          &state, &save);

      GST_BUFFER_SIZE (nal) = nal_len;

      GST_DEBUG_OBJECT (depayload, "adding param %d as %s", i,
          ((GST_BUFFER_DATA (nal)[0] & 0x1f) == 7) ? "SPS" : "PPS");

      gst_rtp_h264_add_sps_pps (rtph264depay, nal);
    }
    g_strfreev (params);

    if (rtph264depay->sps->len == 0 || rtph264depay->pps->len == 0)
      goto incomplete_caps;

  }

  return gst_rtp_h264_set_src_caps (rtph264depay);

  /* ERRORS */
incomplete_caps:
  {
    GST_DEBUG_OBJECT (depayload, "we have incomplete caps,"
        " doing setcaps later");
    return TRUE;
  }
}

static GstBuffer *
gst_rtp_h264_complete_au (GstRtpH264Depay * rtph264depay,
    GstClockTime * out_timestamp, gboolean * out_keyframe)
{
  guint outsize;
  GstBuffer *outbuf;

  /* we had a picture in the adapter and we completed it */
  GST_DEBUG_OBJECT (rtph264depay, "taking completed AU");
  outsize = gst_adapter_available (rtph264depay->picture_adapter);
  outbuf = gst_adapter_take_buffer (rtph264depay->picture_adapter, outsize);

  *out_timestamp = rtph264depay->last_ts;
  *out_keyframe = rtph264depay->last_keyframe;

  rtph264depay->last_keyframe = FALSE;
  rtph264depay->picture_start = FALSE;

  return outbuf;
}

/* SPS/PPS/IDR considered key, all others DELTA;
 * so downstream waiting for keyframe can pick up at SPS/PPS/IDR */
#define NAL_TYPE_IS_KEY(nt) (((nt) == 5) || ((nt) == 7) || ((nt) == 8))

static GstBuffer *
gst_rtp_h264_depay_handle_nal (GstRtpH264Depay * rtph264depay, GstBuffer * nal,
    GstClockTime in_timestamp, gboolean marker)
{
  GstBaseRTPDepayload *depayload = GST_BASE_RTP_DEPAYLOAD (rtph264depay);
  gint nal_type;
  guint size;
  guint8 *data;
  GstBuffer *outbuf = NULL;
  GstClockTime out_timestamp;
  gboolean keyframe, out_keyframe;

  size = GST_BUFFER_SIZE (nal);
  if (G_UNLIKELY (size < 5))
    goto short_nal;

  data = GST_BUFFER_DATA (nal);

  nal_type = data[4] & 0x1f;
  GST_DEBUG_OBJECT (rtph264depay, "handle NAL type %d", nal_type);

  keyframe = NAL_TYPE_IS_KEY (nal_type);

  out_keyframe = keyframe;
  out_timestamp = in_timestamp;

  if (!rtph264depay->byte_stream) {
    if (nal_type == 7 || nal_type == 8) {
      gst_rtp_h264_add_sps_pps (rtph264depay, gst_buffer_create_sub (nal, 4,
              GST_BUFFER_SIZE (nal) - 4));
      gst_buffer_unref (nal);
      return NULL;
    } else if (rtph264depay->sps->len == 0 || rtph264depay->pps->len == 0) {
      /* Down push down any buffer in non-bytestream mode if the SPS/PPS haven't
       * go through yet
       */
      gst_pad_push_event (GST_BASE_RTP_DEPAYLOAD_SINKPAD (depayload),
          gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
              gst_structure_new ("GstForceKeyUnit",
                  "all-headers", G_TYPE_BOOLEAN, TRUE, NULL)));
      gst_buffer_unref (nal);
      return FALSE;
    }

    if (rtph264depay->new_codec_data &&
        rtph264depay->sps->len > 0 && rtph264depay->pps->len > 0)
      gst_rtp_h264_set_src_caps (rtph264depay);
  }


  if (rtph264depay->merge) {
    gboolean start = FALSE, complete = FALSE;

    /* consider a coded slices (IDR or not) to start a picture,
     * (so ending the previous one) if first_mb_in_slice == 0
     * (non-0 is part of previous one) */
    /* NOTE this is not entirely according to Access Unit specs in 7.4.1.2.4,
     * but in practice it works in sane cases, needs not much parsing,
     * and also works with broken frame_num in NAL (where spec-wise would fail) */
    if (nal_type == 1 || nal_type == 2 || nal_type == 5) {
      /* we have a picture start */
      start = TRUE;
      if (data[5] & 0x80) {
        /* first_mb_in_slice == 0 completes a picture */
        complete = TRUE;
      }
    } else if (nal_type >= 6 && nal_type <= 9) {
      /* SEI, SPS, PPS, AU terminate picture */
      complete = TRUE;
    }
    GST_DEBUG_OBJECT (depayload, "start %d, complete %d", start, complete);

    if (complete && rtph264depay->picture_start)
      outbuf = gst_rtp_h264_complete_au (rtph264depay, &out_timestamp,
          &out_keyframe);

    /* add to adapter */
    GST_DEBUG_OBJECT (depayload, "adding NAL to picture adapter");
    gst_adapter_push (rtph264depay->picture_adapter, nal);
    rtph264depay->last_ts = in_timestamp;
    rtph264depay->last_keyframe |= keyframe;
    rtph264depay->picture_start |= start;

    if (marker)
      outbuf = gst_rtp_h264_complete_au (rtph264depay, &out_timestamp,
          &out_keyframe);
  } else {
    /* no merge, output is input nal */
    GST_DEBUG_OBJECT (depayload, "using NAL as output");
    outbuf = nal;
  }

  if (outbuf) {
    /* prepend codec_data */
    if (rtph264depay->codec_data) {
      GST_DEBUG_OBJECT (depayload, "prepending codec_data");
      outbuf = gst_buffer_join (rtph264depay->codec_data, outbuf);
      rtph264depay->codec_data = NULL;
      out_keyframe = TRUE;
    }
    outbuf = gst_buffer_make_metadata_writable (outbuf);

    GST_BUFFER_TIMESTAMP (outbuf) = out_timestamp;

    if (out_keyframe)
      GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));
  }

  return outbuf;

  /* ERRORS */
short_nal:
  {
    GST_WARNING_OBJECT (depayload, "dropping short NAL");
    gst_buffer_unref (nal);
    return NULL;
  }
}

static GstBuffer *
gst_rtp_h264_push_fragmentation_unit (GstRtpH264Depay * rtph264depay,
    gboolean send)
{
  guint outsize;
  guint8 *outdata;
  GstBuffer *outbuf;

  outsize = gst_adapter_available (rtph264depay->adapter);
  outbuf = gst_adapter_take_buffer (rtph264depay->adapter, outsize);
  outdata = GST_BUFFER_DATA (outbuf);

  GST_DEBUG_OBJECT (rtph264depay, "output %d bytes", outsize);

  if (rtph264depay->byte_stream) {
    memcpy (outdata, sync_bytes, sizeof (sync_bytes));
  } else {
    outsize -= 4;
    outdata[0] = (outsize >> 24);
    outdata[1] = (outsize >> 16);
    outdata[2] = (outsize >> 8);
    outdata[3] = (outsize);
  }

  rtph264depay->current_fu_type = 0;

  if (send) {
    outbuf = gst_rtp_h264_depay_handle_nal (rtph264depay, outbuf,
        rtph264depay->fu_timestamp, rtph264depay->fu_marker);
    if (outbuf)
      gst_base_rtp_depayload_push (GST_BASE_RTP_DEPAYLOAD (rtph264depay),
          outbuf);
    return NULL;
  } else {
    return gst_rtp_h264_depay_handle_nal (rtph264depay, outbuf,
        rtph264depay->fu_timestamp, rtph264depay->fu_marker);
  }
}

static GstBuffer *
gst_rtp_h264_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpH264Depay *rtph264depay;
  GstBuffer *outbuf = NULL;
  guint8 nal_unit_type;

  rtph264depay = GST_RTP_H264_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    gst_adapter_clear (rtph264depay->adapter);
    rtph264depay->wait_start = TRUE;
    rtph264depay->current_fu_type = 0;
  }

  {
    gint payload_len;
    guint8 *payload;
    guint header_len;
    guint8 nal_ref_idc;
    guint8 *outdata;
    guint outsize, nalu_size;
    GstClockTime timestamp;
    gboolean marker;

    timestamp = GST_BUFFER_TIMESTAMP (buf);

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);
    marker = gst_rtp_buffer_get_marker (buf);

    GST_DEBUG_OBJECT (rtph264depay, "receiving %d bytes", payload_len);

    if (payload_len == 0)
      return NULL;

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

    /* If FU unit was being processed, but the current nal is of a different
     * type.  Assume that the remote payloader is buggy (didn't set the end bit
     * when the FU ended) and send out what we gathered thusfar */
    if (G_UNLIKELY (rtph264depay->current_fu_type != 0 &&
            nal_unit_type != rtph264depay->current_fu_type))
      gst_rtp_h264_push_fragmentation_unit (rtph264depay, TRUE);

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


        /* STAP-A    Single-time aggregation packet     5.7.1 */
        while (payload_len > 2) {
          /*                      1          
           *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           * |         NALU Size             |
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           */
          nalu_size = (payload[0] << 8) | payload[1];

          /* dont include nalu_size */
          if (nalu_size > (payload_len - 2))
            nalu_size = payload_len - 2;

          outsize = nalu_size + sizeof (sync_bytes);
          outbuf = gst_buffer_new_and_alloc (outsize);
          outdata = GST_BUFFER_DATA (outbuf);
          if (rtph264depay->byte_stream) {
            memcpy (outdata, sync_bytes, sizeof (sync_bytes));
          } else {
            outdata[0] = outdata[1] = 0;
            outdata[2] = payload[0];
            outdata[3] = payload[1];
          }

          /* strip NALU size */
          payload += 2;
          payload_len -= 2;

          outdata += sizeof (sync_bytes);
          memcpy (outdata, payload, nalu_size);

          gst_adapter_push (rtph264depay->adapter, outbuf);

          payload += nalu_size;
          payload_len -= nalu_size;
        }

        outsize = gst_adapter_available (rtph264depay->adapter);
        outbuf = gst_adapter_take_buffer (rtph264depay->adapter, outsize);

        outbuf = gst_rtp_h264_depay_handle_nal (rtph264depay, outbuf, timestamp,
            marker);
        break;
      }
      case 26:
        /* MTAP16    Multi-time aggregation packet      5.7.2 */
        header_len = 5;
        /* fallthrough, not implemented */
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

          /* If a new FU unit started, while still processing an older one.
           * Assume that the remote payloader is buggy (doesn't set the end
           * bit) and send out what we've gathered thusfar */
          if (G_UNLIKELY (rtph264depay->current_fu_type != 0))
            gst_rtp_h264_push_fragmentation_unit (rtph264depay, TRUE);

          rtph264depay->current_fu_type = nal_unit_type;
          rtph264depay->fu_timestamp = timestamp;

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

        outbuf = NULL;
        rtph264depay->fu_marker = marker;

        /* if NAL unit ends, flush the adapter */
        if (E)
          outbuf = gst_rtp_h264_push_fragmentation_unit (rtph264depay, FALSE);
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
        if (rtph264depay->byte_stream) {
          memcpy (outdata, sync_bytes, sizeof (sync_bytes));
        } else {
          outdata[0] = outdata[1] = 0;
          outdata[2] = nalu_size >> 8;
          outdata[3] = nalu_size & 0xff;
        }
        outdata += sizeof (sync_bytes);
        memcpy (outdata, payload, nalu_size);

        outbuf = gst_rtp_h264_depay_handle_nal (rtph264depay, outbuf, timestamp,
            marker);
        break;
      }
    }
  }

  return outbuf;

  /* ERRORS */
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

static gboolean
gst_rtp_h264_depay_handle_event (GstBaseRTPDepayload * depay, GstEvent * event)
{
  GstRtpH264Depay *rtph264depay;

  rtph264depay = GST_RTP_H264_DEPAY (depay);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_rtp_h264_depay_reset (rtph264depay);
      break;
    default:
      break;
  }

  return
      GST_BASE_RTP_DEPAYLOAD_CLASS (parent_class)->handle_event (depay, event);
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
      gst_rtp_h264_depay_reset (rtph264depay);
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
      GST_RANK_SECONDARY, GST_TYPE_RTP_H264_DEPAY);
}
