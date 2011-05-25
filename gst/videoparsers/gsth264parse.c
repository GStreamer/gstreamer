/* GStreamer H.264 Parser
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2010> Collabora Multimedia
 * Copyright (C) <2010> Nokia Corporation
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

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <gst/base/gstadapter.h>
#include "gsth264parse.h"

#include <string.h>

GST_DEBUG_CATEGORY (h264_parse_debug);
#define GST_CAT_DEFAULT h264_parse_debug

#define DEFAULT_CONFIG_INTERVAL      (0)

enum
{
  PROP_0,
  PROP_CONFIG_INTERVAL,
  PROP_LAST
};

enum
{
  GST_H264_PARSE_FORMAT_NONE,
  GST_H264_PARSE_FORMAT_AVC,
  GST_H264_PARSE_FORMAT_BYTE
};

enum
{
  GST_H264_PARSE_ALIGN_NONE = 0,
  GST_H264_PARSE_ALIGN_NAL,
  GST_H264_PARSE_ALIGN_AU
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, parsed = (boolean) false"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, parsed = (boolean) true"));

GST_BOILERPLATE (GstH264Parse, gst_h264_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static void gst_h264_parse_finalize (GObject * object);

static gboolean gst_h264_parse_start (GstBaseParse * parse);
static gboolean gst_h264_parse_stop (GstBaseParse * parse);
static gboolean gst_h264_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize);
static GstFlowReturn gst_h264_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static GstFlowReturn gst_h264_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);

static void gst_h264_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h264_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_h264_parse_set_caps (GstBaseParse * parse, GstCaps * caps);
static GstFlowReturn gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer);

static void
gst_h264_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details_simple (gstelement_class, "H.264 parser",
      "Codec/Parser/Video",
      "Parses H.264 streams",
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (h264_parse_debug, "h264parse", 0, "h264 parser");
}

static void
gst_h264_parse_class_init (GstH264ParseClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  gobject_class->finalize = gst_h264_parse_finalize;
  gobject_class->set_property = gst_h264_parse_set_property;
  gobject_class->get_property = gst_h264_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_CONFIG_INTERVAL,
      g_param_spec_uint ("config-interval",
          "SPS PPS Send Interval",
          "Send SPS and PPS Insertion Interval in seconds (sprop parameter sets "
          "will be multiplexed in the data stream when detected.) (0 = disabled)",
          0, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* Override BaseParse vfuncs */
  parse_class->start = GST_DEBUG_FUNCPTR (gst_h264_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_h264_parse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_h264_parse_check_valid_frame);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_h264_parse_parse_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_h264_parse_pre_push_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_h264_parse_set_caps);
}

static void
gst_h264_parse_init (GstH264Parse * h264parse, GstH264ParseClass * g_class)
{
  h264parse->frame_out = gst_adapter_new ();

  /* retrieve and intercept baseparse.
   * Quite HACKish, but fairly OK since it is needed to perform avc packet
   * splitting, which is the penultimate de-parsing */
  h264parse->parse_chain =
      GST_PAD_CHAINFUNC (GST_BASE_PARSE_SINK_PAD (h264parse));
  gst_pad_set_chain_function (GST_BASE_PARSE_SINK_PAD (h264parse),
      gst_h264_parse_chain);
}


static void
gst_h264_parse_finalize (GObject * object)
{
  GstH264Parse *h264parse = GST_H264_PARSE (object);

  g_object_unref (h264parse->frame_out);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h264_parse_reset_frame (GstH264Parse * h264parse)
{
  /* done parsing; reset state */
  h264parse->last_nal_pos = 0;
  h264parse->next_sc_pos = 0;
  h264parse->picture_start = FALSE;
  h264parse->update_caps = FALSE;
  h264parse->idr_pos = -1;
  h264parse->keyframe = FALSE;
  h264parse->frame_start = FALSE;
  gst_adapter_clear (h264parse->frame_out);
}

static void
gst_h264_parse_reset (GstH264Parse * h264parse)
{
  h264parse->width = 0;
  h264parse->height = 0;
  h264parse->fps_num = 0;
  h264parse->fps_den = 0;
  gst_buffer_replace (&h264parse->codec_data, NULL);
  h264parse->nal_length_size = 4;
  h264parse->packetized = FALSE;

  h264parse->align = GST_H264_PARSE_ALIGN_NONE;
  h264parse->format = GST_H264_PARSE_FORMAT_NONE;

  h264parse->last_report = GST_CLOCK_TIME_NONE;
  h264parse->push_codec = FALSE;

  gst_h264_parse_reset_frame (h264parse);
}

static gboolean
gst_h264_parse_start (GstBaseParse * parse)
{
  GstH264Parse *h264parse = GST_H264_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");
  gst_h264_parse_reset (h264parse);

  gst_h264_params_create (&h264parse->params, GST_ELEMENT (h264parse));

  gst_base_parse_set_min_frame_size (parse, 6);

  return TRUE;
}

static gboolean
gst_h264_parse_stop (GstBaseParse * parse)
{
  GstH264Parse *h264parse = GST_H264_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");
  gst_h264_parse_reset (h264parse);

  gst_h264_params_free (h264parse->params);
  h264parse->params = NULL;

  return TRUE;
}

static const gchar *
gst_h264_parse_get_string (GstH264Parse * parse, gboolean format, gint code)
{
  if (format) {
    switch (code) {
      case GST_H264_PARSE_FORMAT_AVC:
        return "avc";
      case GST_H264_PARSE_FORMAT_BYTE:
        return "byte-stream";
      default:
        return "none";
    }
  } else {
    switch (code) {
      case GST_H264_PARSE_ALIGN_NAL:
        return "nal";
      case GST_H264_PARSE_ALIGN_AU:
        return "au";
      default:
        return "none";
    }
  }
}

static void
gst_h264_parse_format_from_caps (GstCaps * caps, guint * format, guint * align)
{

  if (format)
    *format = GST_H264_PARSE_FORMAT_NONE;

  if (align)
    *align = GST_H264_PARSE_ALIGN_NONE;

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if (format) {
      if ((str = gst_structure_get_string (s, "stream-format"))) {
        if (strcmp (str, "avc") == 0)
          *format = GST_H264_PARSE_FORMAT_AVC;
        else if (strcmp (str, "byte-stream") == 0)
          *format = GST_H264_PARSE_FORMAT_BYTE;
      }
    }

    if (align) {
      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0)
          *align = GST_H264_PARSE_ALIGN_AU;
        else if (strcmp (str, "nal") == 0)
          *align = GST_H264_PARSE_ALIGN_NAL;
      }
    }
  }
}

/* check downstream caps to configure format and alignment */
static void
gst_h264_parse_negotiate (GstH264Parse * h264parse)
{
  GstCaps *caps;
  guint format = GST_H264_PARSE_FORMAT_NONE;
  guint align = GST_H264_PARSE_ALIGN_NONE;

  caps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (h264parse));
  GST_DEBUG_OBJECT (h264parse, "allowed caps: %" GST_PTR_FORMAT, caps);

  gst_h264_parse_format_from_caps (caps, &format, &align);

  if (caps)
    gst_caps_unref (caps);

  /* default */
  if (!format)
    format = GST_H264_PARSE_FORMAT_BYTE;
  if (!align)
    align = GST_H264_PARSE_ALIGN_AU;

  GST_DEBUG_OBJECT (h264parse, "selected format %s, alignment %s",
      gst_h264_parse_get_string (h264parse, TRUE, format),
      gst_h264_parse_get_string (h264parse, FALSE, align));

  h264parse->format = format;
  h264parse->align = align;
}

static GstBuffer *
gst_h264_parse_wrap_nal (GstH264Parse * h264parse, guint format, guint8 * data,
    guint size)
{
  GstBuffer *buf;
  const guint nl = h264parse->nal_length_size;

  buf = gst_buffer_new_and_alloc (size + nl + 4);
  if (format == GST_H264_PARSE_FORMAT_AVC) {
    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), size << (32 - 8 * nl));
  } else {
    g_assert (nl == 4);
    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), 1);
  }

  GST_BUFFER_SIZE (buf) = size + nl;
  memcpy (GST_BUFFER_DATA (buf) + nl, data, size);

  return buf;
}

/* SPS/PPS/IDR considered key, all others DELTA;
 * so downstream waiting for keyframe can pick up at SPS/PPS/IDR */
#define NAL_TYPE_IS_KEY(nt) (((nt) == 5) || ((nt) == 7) || ((nt) == 8))

/* caller guarantees 2 bytes of nal payload */
static void
gst_h264_parse_process_nal (GstH264Parse * h264parse, guint8 * data,
    gint sc_pos, gint nal_pos, guint nal_size)
{
  guint nal_type;

  g_return_if_fail (nal_pos - sc_pos > 0 && nal_pos - sc_pos <= 4);

  /* nothing to do for broken input */
  if (G_UNLIKELY (nal_size < 2)) {
    GST_DEBUG_OBJECT (h264parse, "not processing nal size %u", nal_size);
    return;
  }

  /* lower layer collects params */
  gst_h264_params_parse_nal (h264parse->params, data + nal_pos, nal_size);

  /* we have a peek as well */
  nal_type = data[nal_pos] & 0x1f;
  h264parse->keyframe |= NAL_TYPE_IS_KEY (nal_type);

  switch (nal_type) {
    case NAL_SPS:
    case NAL_PPS:
      /* parameters might have changed, force caps check */
      GST_DEBUG_OBJECT (h264parse, "triggering src caps check");
      h264parse->update_caps = TRUE;
      /* found in stream, no need to forcibly push at start */
      h264parse->push_codec = FALSE;
      break;
    case NAL_SLICE:
    case NAL_SLICE_DPA:
    case NAL_SLICE_DPB:
    case NAL_SLICE_DPC:
      /* real frame data */
      h264parse->frame_start |= (h264parse->params->first_mb_in_slice == 0);
      /* if we need to sneak codec NALs into the stream,
       * this is a good place, so fake it as IDR
       * (which should be at start anyway) */
      if (G_LIKELY (!h264parse->push_codec))
        break;
      /* fall-through */
    case NAL_SLICE_IDR:
      /* real frame data */
      h264parse->frame_start |= (h264parse->params->first_mb_in_slice == 0);
      /* mark where config needs to go if interval expired */
      /* mind replacement buffer if applicable */
      if (h264parse->format == GST_H264_PARSE_FORMAT_AVC)
        h264parse->idr_pos = gst_adapter_available (h264parse->frame_out);
      else
        h264parse->idr_pos = sc_pos;
      GST_DEBUG_OBJECT (h264parse, "marking IDR in frame at offset %d",
          h264parse->idr_pos);
      break;
  }

  /* if AVC output needed, collect properly prefixed nal in adapter,
   * and use that to replace outgoing buffer data later on */
  if (h264parse->format == GST_H264_PARSE_FORMAT_AVC) {
    GstBuffer *buf;

    GST_LOG_OBJECT (h264parse, "collecting NAL in AVC frame");
    buf = gst_h264_parse_wrap_nal (h264parse, h264parse->format,
        data + nal_pos, nal_size);
    gst_adapter_push (h264parse->frame_out, buf);
  }
}

/* caller guarantees at least 2 bytes of nal payload for each nal
 * returns TRUE if next_nal indicates that nal terminates an AU */
static inline gboolean
gst_h264_parse_collect_nal (GstH264Parse * h264parse, guint8 * nal,
    guint8 * next_nal)
{
  gint nal_type;
  gboolean complete;

  if (h264parse->align == GST_H264_PARSE_ALIGN_NAL)
    return TRUE;

  /* determine if AU complete */
  nal_type = nal[0] & 0x1f;
  GST_LOG_OBJECT (h264parse, "nal type: %d", nal_type);
  /* coded slice NAL starts a picture,
   * i.e. other types become aggregated in front of it */
  h264parse->picture_start |= (nal_type == 1 || nal_type == 2 || nal_type == 5);

  /* consider a coded slices (IDR or not) to start a picture,
   * (so ending the previous one) if first_mb_in_slice == 0
   * (non-0 is part of previous one) */
  /* NOTE this is not entirely according to Access Unit specs in 7.4.1.2.4,
   * but in practice it works in sane cases, needs not much parsing,
   * and also works with broken frame_num in NAL
   * (where spec-wise would fail) */
  nal_type = next_nal[0] & 0x1f;
  GST_LOG_OBJECT (h264parse, "next nal type: %d", nal_type);
  complete = h264parse->picture_start && (nal_type >= 6 && nal_type <= 9);
  complete |= h264parse->picture_start &&
      (nal_type == 1 || nal_type == 2 || nal_type == 5) &&
      /* first_mb_in_slice == 0 considered start of frame */
      (next_nal[1] & 0x80);

  GST_LOG_OBJECT (h264parse, "au complete: %d", complete);

  return complete;
}

/* finds next startcode == 00 00 01, along with a subsequent byte */
static guint
gst_h264_parse_find_sc (GstBuffer * buffer, guint skip)
{
  GstByteReader br;
  guint sc_pos = -1;

  gst_byte_reader_init_from_buffer (&br, buffer);

  /* NALU not empty, so we can at least expect 1 (even 2) bytes following sc */
  sc_pos = gst_byte_reader_masked_scan_uint32 (&br, 0xffffff00, 0x00000100,
      skip, gst_byte_reader_get_remaining (&br) - skip);

  return sc_pos;
}

/* FIXME move into baseparse, or anything equivalent;
 * see https://bugzilla.gnome.org/show_bug.cgi?id=650093 */
#define GST_BASE_PARSE_FRAME_FLAG_PARSING   0x10000

static gboolean
gst_h264_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstH264Parse *h264parse = GST_H264_PARSE (parse);
  GstBuffer *buffer = frame->buffer;
  gint sc_pos, nal_pos, next_sc_pos, next_nal_pos;
  guint8 *data;
  guint size;
  gboolean drain;

  /* expect at least 3 bytes startcode == sc, and 2 bytes NALU payload */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buffer) < 5))
    return FALSE;

  /* need to configure aggregation */
  if (G_UNLIKELY (h264parse->format == GST_H264_PARSE_FORMAT_NONE))
    gst_h264_parse_negotiate (h264parse);

  /* avoid stale cached parsing state */
  if (!(frame->flags & GST_BASE_PARSE_FRAME_FLAG_PARSING)) {
    GST_LOG_OBJECT (h264parse, "parsing new frame");
    gst_h264_parse_reset_frame (h264parse);
    frame->flags |= GST_BASE_PARSE_FRAME_FLAG_PARSING;
  } else {
    GST_LOG_OBJECT (h264parse, "resuming frame parsing");
  }

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  GST_LOG_OBJECT (h264parse, "last_nal_pos: %d, last_scan_pos %d",
      h264parse->last_nal_pos, h264parse->next_sc_pos);

  nal_pos = h264parse->last_nal_pos;
  next_sc_pos = h264parse->next_sc_pos;

  if (!next_sc_pos) {
    sc_pos = gst_h264_parse_find_sc (buffer, 0);

    if (sc_pos == -1) {
      /* SC not found, need more data */
      sc_pos = GST_BUFFER_SIZE (buffer) - 3;
      goto more;
    }

    nal_pos = sc_pos + 3;
    next_sc_pos = nal_pos;
    /* sc might have 2 or 3 0-bytes */
    if (sc_pos > 0 && data[sc_pos - 1] == 00)
      sc_pos--;
    GST_LOG_OBJECT (h264parse, "found sc at offset %d", sc_pos);
  } else {
    /* previous checks already arrange sc at start */
    sc_pos = 0;
  }

  drain = GST_BASE_PARSE_DRAINING (parse);
  while (TRUE) {
    gint prev_sc_pos;

    next_sc_pos = gst_h264_parse_find_sc (buffer, next_sc_pos);
    if (next_sc_pos == -1) {
      GST_LOG_OBJECT (h264parse, "no next sc");
      if (drain) {
        /* FLUSH/EOS, it's okay if we can't find the next frame */
        next_sc_pos = size;
        next_nal_pos = size;
      } else {
        next_sc_pos = size - 3;
        goto more;
      }
    } else {
      next_nal_pos = next_sc_pos + 3;
      if (data[next_sc_pos - 1] == 00)
        next_sc_pos--;
      GST_LOG_OBJECT (h264parse, "found next sc at offset %d", next_sc_pos);
      /* need at least 1 more byte of next NAL */
      if (!drain && (next_nal_pos == size - 1))
        goto more;
    }

    /* determine nal's sc position */
    prev_sc_pos = nal_pos - 3;
    g_assert (prev_sc_pos >= 0);
    if (prev_sc_pos > 0 && data[prev_sc_pos - 1] == 0)
      prev_sc_pos--;

    /* already consume and gather info from NAL */
    if (G_UNLIKELY (next_sc_pos - nal_pos < 2)) {
      GST_WARNING_OBJECT (h264parse, "input stream is corrupt; "
          "it contains a NAL unit of length %d", next_sc_pos - nal_pos);
      /* broken nal at start -> arrange to skip it,
       * otherwise have it terminate current au
       * (and so it will be skippd on next frame round) */
      if (prev_sc_pos == sc_pos) {
        *skipsize = sc_pos + 2;
        return FALSE;
      } else {
        next_sc_pos = prev_sc_pos;
        break;
      }
    }
    gst_h264_parse_process_nal (h264parse, data, prev_sc_pos, nal_pos,
        next_sc_pos - nal_pos);
    if (next_nal_pos >= size - 1 ||
        gst_h264_parse_collect_nal (h264parse, data + nal_pos,
            data + next_nal_pos))
      break;

    /* move along */
    next_sc_pos = nal_pos = next_nal_pos;
  }

  *skipsize = sc_pos;
  *framesize = next_sc_pos - sc_pos;

  return TRUE;

more:
  /* ask for best next available */
  *framesize = G_MAXUINT;

  /* skip up to initial startcode */
  *skipsize = sc_pos;
  /* resume scanning here next time */
  h264parse->last_nal_pos = nal_pos - sc_pos;
  h264parse->next_sc_pos = next_sc_pos - sc_pos;

  return FALSE;
}

/* byte together avc codec data based on collected pps and sps so far */
static GstBuffer *
gst_h264_parse_make_codec_data (GstH264Parse * h264parse)
{
  GstBuffer *buf, *nal;
  gint i, sps_size = 0, pps_size = 0, num_sps = 0, num_pps = 0;
  guint8 profile_idc = 0, profile_comp = 0, level_idc = 0;
  gboolean found = FALSE;
  guint8 *data;

  /* only nal payload in stored nals */

  for (i = 0; i < MAX_SPS_COUNT; i++) {
    if ((nal = h264parse->params->sps_nals[i])) {
      num_sps++;
      /* size bytes also count */
      sps_size += GST_BUFFER_SIZE (nal) + 2;
      if (GST_BUFFER_SIZE (nal) >= 4) {
        found = TRUE;
        profile_idc = (GST_BUFFER_DATA (nal))[1];
        profile_comp = (GST_BUFFER_DATA (nal))[2];
        level_idc = (GST_BUFFER_DATA (nal))[3];
      }
    }
  }
  for (i = 0; i < MAX_PPS_COUNT; i++) {
    if ((nal = h264parse->params->pps_nals[i])) {
      num_pps++;
      /* size bytes also count */
      pps_size += GST_BUFFER_SIZE (nal) + 2;
    }
  }

  GST_DEBUG_OBJECT (h264parse,
      "constructing codec_data: num_sps=%d, num_pps=%d", num_sps, num_pps);

  if (!found || !num_pps)
    return NULL;

  buf = gst_buffer_new_and_alloc (5 + 1 + sps_size + 1 + pps_size);
  data = GST_BUFFER_DATA (buf);

  data[0] = 1;                  /* AVC Decoder Configuration Record ver. 1 */
  data[1] = profile_idc;        /* profile_idc                             */
  data[2] = profile_comp;       /* profile_compability                     */
  data[3] = level_idc;          /* level_idc                               */
  data[4] = 0xfc | (4 - 1);     /* nal_length_size_minus1                  */
  data[5] = 0xe0 | num_sps;     /* number of SPSs */

  data += 6;
  for (i = 0; i < MAX_SPS_COUNT; i++) {
    if ((nal = h264parse->params->sps_nals[i])) {
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (nal));
      memcpy (data + 2, GST_BUFFER_DATA (nal), GST_BUFFER_SIZE (nal));
      data += 2 + GST_BUFFER_SIZE (nal);
    }
  }

  data[0] = num_pps;
  data++;
  for (i = 0; i < MAX_PPS_COUNT; i++) {
    if ((nal = h264parse->params->pps_nals[i])) {
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (nal));
      memcpy (data + 2, GST_BUFFER_DATA (nal), GST_BUFFER_SIZE (nal));
      data += 2 + GST_BUFFER_SIZE (nal);
    }
  }

  return buf;
}

static void
gst_h264_parse_update_src_caps (GstH264Parse * h264parse, GstCaps * caps)
{
  GstH264ParamsSPS *sps;
  GstCaps *sink_caps;
  gboolean modified = FALSE;
  GstBuffer *buf = NULL;

  if (G_UNLIKELY (!GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (h264parse))))
    modified = TRUE;
  else if (G_UNLIKELY (!h264parse->update_caps))
    return;

  /* if this is being called from the first _setcaps call, caps on the sinkpad
   * aren't set yet and so they need to be passed as an argument */
  if (caps)
    sink_caps = caps;
  else
    sink_caps = GST_PAD_CAPS (GST_BASE_PARSE_SINK_PAD (h264parse));

  /* carry over input caps as much as possible; override with our own stuff */
  if (sink_caps)
    gst_caps_ref (sink_caps);
  else
    sink_caps = gst_caps_new_simple ("video/x-h264", NULL);

  sps = h264parse->params->sps;
  GST_DEBUG_OBJECT (h264parse, "sps: %p", sps);

  /* only codec-data for nice-and-clean au aligned packetized avc format */
  if (h264parse->format == GST_H264_PARSE_FORMAT_AVC &&
      h264parse->align == GST_H264_PARSE_ALIGN_AU) {
    buf = gst_h264_parse_make_codec_data (h264parse);
    if (buf && h264parse->codec_data) {
      if (GST_BUFFER_SIZE (buf) != GST_BUFFER_SIZE (h264parse->codec_data) ||
          memcmp (GST_BUFFER_DATA (buf),
              GST_BUFFER_DATA (h264parse->codec_data), GST_BUFFER_SIZE (buf)))
        modified = TRUE;
    } else {
      if (h264parse->codec_data)
        buf = gst_buffer_ref (h264parse->codec_data);
      modified = TRUE;
    }
  }

  caps = NULL;
  if (G_UNLIKELY (!sps)) {
    caps = gst_caps_copy (sink_caps);
  } else if (G_UNLIKELY (h264parse->width != sps->width ||
          h264parse->height != sps->height || h264parse->fps_num != sps->fps_num
          || h264parse->fps_den != sps->fps_den || modified)) {
    caps = gst_caps_copy (sink_caps);
    /* sps should give this */
    gst_caps_set_simple (caps, "width", G_TYPE_INT, sps->width,
        "height", G_TYPE_INT, sps->height, NULL);
    h264parse->height = sps->height;
    h264parse->width = sps->width;
    /* but not necessarily or reliably this */
    if ((!h264parse->fps_num || !h264parse->fps_den) &&
        sps->fps_num > 0 && sps->fps_den > 0) {
      gst_caps_set_simple (caps, "framerate",
          GST_TYPE_FRACTION, sps->fps_num, sps->fps_den, NULL);
      h264parse->fps_num = sps->fps_num;
      h264parse->fps_den = sps->fps_den;
      gst_base_parse_set_frame_rate (GST_BASE_PARSE (h264parse),
          h264parse->fps_num, h264parse->fps_den, 0, 0);
    }
  }

  if (caps) {
    gst_caps_set_simple (caps, "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h264_parse_get_string (h264parse, TRUE, h264parse->format),
        "alignment", G_TYPE_STRING,
        gst_h264_parse_get_string (h264parse, FALSE, h264parse->align), NULL);
    if (buf) {
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
      gst_buffer_replace (&h264parse->codec_data, buf);
      gst_buffer_unref (buf);
      buf = NULL;
    } else {
      GstStructure *s;
      /* remove any left-over codec-data hanging around */
      s = gst_caps_get_structure (caps, 0);
      gst_structure_remove_field (s, "codec_data");
    }
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (h264parse), caps);
    gst_caps_unref (caps);
  }

  gst_caps_unref (sink_caps);
  if (buf)
    gst_buffer_unref (buf);
}

static GstFlowReturn
gst_h264_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstH264Parse *h264parse;
  GstBuffer *buffer;
  guint av;

  h264parse = GST_H264_PARSE (parse);
  buffer = frame->buffer;

  gst_h264_parse_update_src_caps (h264parse, NULL);

  gst_h264_params_get_timestamp (h264parse->params,
      &GST_BUFFER_TIMESTAMP (buffer), &GST_BUFFER_DURATION (buffer),
      h264parse->frame_start);

  if (h264parse->keyframe)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* replace with transformed AVC output if applicable */
  av = gst_adapter_available (h264parse->frame_out);
  if (av) {
    GstBuffer *buf;

    buf = gst_adapter_take_buffer (h264parse->frame_out, av);
    gst_buffer_copy_metadata (buf, buffer, GST_BUFFER_COPY_ALL);
    gst_buffer_replace (&frame->buffer, buf);
    gst_buffer_unref (buf);
  }

  return GST_FLOW_OK;
}

/* sends a codec NAL downstream, decorating and transforming as needed.
 * No ownership is taken of @nal */
static GstFlowReturn
gst_h264_parse_push_codec_buffer (GstH264Parse * h264parse, GstBuffer * nal,
    GstClockTime ts)
{
  nal = gst_h264_parse_wrap_nal (h264parse, h264parse->format,
      GST_BUFFER_DATA (nal), GST_BUFFER_SIZE (nal));

  GST_BUFFER_TIMESTAMP (nal) = ts;
  GST_BUFFER_DURATION (nal) = 0;

  gst_buffer_set_caps (nal, GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (h264parse)));

  return gst_pad_push (GST_BASE_PARSE_SRC_PAD (h264parse), nal);
}

static GstFlowReturn
gst_h264_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstH264Parse *h264parse;
  GstBuffer *buffer;

  h264parse = GST_H264_PARSE (parse);
  buffer = frame->buffer;

  /* periodic SPS/PPS sending */
  if (h264parse->interval > 0 || h264parse->push_codec) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);
    guint64 diff;

    /* init */
    if (!GST_CLOCK_TIME_IS_VALID (h264parse->last_report)) {
      h264parse->last_report = timestamp;
    }

    if (h264parse->idr_pos >= 0) {
      GST_LOG_OBJECT (h264parse, "IDR nal at offset %d", h264parse->idr_pos);

      if (timestamp > h264parse->last_report)
        diff = timestamp - h264parse->last_report;
      else
        diff = 0;

      GST_LOG_OBJECT (h264parse,
          "now %" GST_TIME_FORMAT ", last SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (h264parse->last_report));

      GST_DEBUG_OBJECT (h264parse,
          "interval since last SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff));

      if (GST_TIME_AS_SECONDS (diff) >= h264parse->interval ||
          h264parse->push_codec) {
        GstBuffer *codec_nal;
        gint i;
        GstClockTime new_ts;

        /* avoid overwriting a perfectly fine timestamp */
        new_ts = GST_CLOCK_TIME_IS_VALID (timestamp) ? timestamp :
            h264parse->last_report;

        if (h264parse->align == GST_H264_PARSE_ALIGN_NAL) {
          /* send separate config NAL buffers */
          GST_DEBUG_OBJECT (h264parse, "- sending SPS/PPS");
          for (i = 0; i < MAX_SPS_COUNT; i++) {
            if ((codec_nal = h264parse->params->sps_nals[i])) {
              GST_DEBUG_OBJECT (h264parse, "sending SPS nal");
              gst_h264_parse_push_codec_buffer (h264parse, codec_nal,
                  timestamp);
              h264parse->last_report = new_ts;
            }
          }
          for (i = 0; i < MAX_PPS_COUNT; i++) {
            if ((codec_nal = h264parse->params->pps_nals[i])) {
              GST_DEBUG_OBJECT (h264parse, "sending PPS nal");
              gst_h264_parse_push_codec_buffer (h264parse, codec_nal,
                  timestamp);
              h264parse->last_report = new_ts;
            }
          }
        } else {
          /* insert config NALs into AU */
          GstByteWriter bw;
          GstBuffer *new_buf;
          const gboolean bs = h264parse->format == GST_H264_PARSE_FORMAT_BYTE;

          gst_byte_writer_init_with_size (&bw, GST_BUFFER_SIZE (buffer), FALSE);
          gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (buffer),
              h264parse->idr_pos);
          GST_DEBUG_OBJECT (h264parse, "- inserting SPS/PPS");
          for (i = 0; i < MAX_SPS_COUNT; i++) {
            if ((codec_nal = h264parse->params->sps_nals[i])) {
              GST_DEBUG_OBJECT (h264parse, "inserting SPS nal");
              gst_byte_writer_put_uint32_be (&bw,
                  bs ? 1 : GST_BUFFER_SIZE (codec_nal));
              gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (codec_nal),
                  GST_BUFFER_SIZE (codec_nal));
              h264parse->last_report = new_ts;
            }
          }
          for (i = 0; i < MAX_PPS_COUNT; i++) {
            if ((codec_nal = h264parse->params->pps_nals[i])) {
              GST_DEBUG_OBJECT (h264parse, "inserting PPS nal");
              gst_byte_writer_put_uint32_be (&bw,
                  bs ? 1 : GST_BUFFER_SIZE (codec_nal));
              gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (codec_nal),
                  GST_BUFFER_SIZE (codec_nal));
              h264parse->last_report = new_ts;
            }
          }
          gst_byte_writer_put_data (&bw,
              GST_BUFFER_DATA (buffer) + h264parse->idr_pos,
              GST_BUFFER_SIZE (buffer) - h264parse->idr_pos);
          /* collect result and push */
          new_buf = gst_byte_writer_reset_and_get_buffer (&bw);
          gst_buffer_copy_metadata (new_buf, buffer, GST_BUFFER_COPY_ALL);
          gst_buffer_replace (&frame->buffer, new_buf);
          gst_buffer_unref (new_buf);
        }
      }
      /* we pushed whatever we had */
      h264parse->push_codec = FALSE;
    }
  }

  gst_h264_parse_reset_frame (h264parse);

  return GST_FLOW_OK;
}

static gboolean
gst_h264_parse_set_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstH264Parse *h264parse;
  GstStructure *str;
  const GValue *value;
  GstBuffer *codec_data = NULL;
  guint size, format, align;

  h264parse = GST_H264_PARSE (parse);

  /* reset */
  h264parse->push_codec = FALSE;

  str = gst_caps_get_structure (caps, 0);

  /* accept upstream info if provided */
  gst_structure_get_int (str, "width", &h264parse->width);
  gst_structure_get_int (str, "height", &h264parse->height);
  gst_structure_get_fraction (str, "framerate", &h264parse->fps_num,
      &h264parse->fps_den);

  /* packetized video has a codec_data */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    guint8 *data;
    guint num_sps, num_pps, profile, len;
    gint i;

    GST_DEBUG_OBJECT (h264parse, "have packetized h264");
    /* make note for optional split processing */
    h264parse->packetized = TRUE;

    codec_data = gst_value_get_buffer (value);
    if (!codec_data)
      goto wrong_type;
    data = GST_BUFFER_DATA (codec_data);
    size = GST_BUFFER_SIZE (codec_data);

    /* parse the avcC data */
    if (size < 7)
      goto avcc_too_small;
    /* parse the version, this must be 1 */
    if (data[0] != 1)
      goto wrong_version;

    /* AVCProfileIndication */
    /* profile_compat */
    /* AVCLevelIndication */
    profile = (data[1] << 16) | (data[2] << 8) | data[3];
    GST_DEBUG_OBJECT (h264parse, "profile %06x", profile);

    /* 6 bits reserved | 2 bits lengthSizeMinusOne */
    /* this is the number of bytes in front of the NAL units to mark their
     * length */
    h264parse->nal_length_size = (data[4] & 0x03) + 1;
    GST_DEBUG_OBJECT (h264parse, "nal length %u", h264parse->nal_length_size);

    num_sps = data[5] & 0x1f;
    data += 6;
    size -= 6;
    for (i = 0; i < num_sps; i++) {
      len = GST_READ_UINT16_BE (data);
      if (size < len + 2 || len < 2)
        goto avcc_too_small;
      /* digest for later reference */
      gst_h264_parse_process_nal (h264parse, data, 0, 2, len);
      data += len + 2;
      size -= len + 2;
    }
    num_pps = data[0];
    data++;
    size++;
    for (i = 0; i < num_pps; i++) {
      len = GST_READ_UINT16_BE (data);
      if (size < len + 2 || len < 2)
        goto avcc_too_small;
      /* digest for later reference */
      gst_h264_parse_process_nal (h264parse, data, 0, 2, len);
      data += len + 2;
      size -= len + 2;
    }

    h264parse->codec_data = gst_buffer_ref (codec_data);
  } else {
    GST_DEBUG_OBJECT (h264parse, "have bytestream h264");
    /* nothing to pre-process */
    h264parse->packetized = FALSE;
    /* we have 4 sync bytes */
    h264parse->nal_length_size = 4;
  }

  /* negotiate with downstream, sets ->format and ->align */
  gst_h264_parse_negotiate (h264parse);

  /* get upstream format and align from caps */
  gst_h264_parse_format_from_caps (caps, &format, &align);

  /* if upstream sets codec_data without setting stream-format and alignment, we
   * assume stream-format=avc,alignment=au */
  if (format == GST_H264_PARSE_FORMAT_NONE) {
    if (codec_data == NULL)
      goto unknown_input_format;

    format = GST_H264_PARSE_FORMAT_AVC;
    align = GST_H264_PARSE_ALIGN_AU;
  }

  if (format == h264parse->format && align == h264parse->align) {
    gst_base_parse_set_passthrough (parse, TRUE);

    /* we did parse codec-data and might supplement src caps */
    gst_h264_parse_update_src_caps (h264parse, caps);
  } else if (format == GST_H264_PARSE_FORMAT_AVC &&
      h264parse->format == GST_H264_PARSE_FORMAT_BYTE) {
    /* arrange to insert codec-data in-stream if needed.
     * src caps are only arranged for later on */
    h264parse->push_codec = TRUE;
    h264parse->split_packetized = TRUE;
  }

  return TRUE;

  /* ERRORS */
avcc_too_small:
  {
    GST_DEBUG_OBJECT (h264parse, "avcC size %u < 7", size);
    goto refuse_caps;
  }
wrong_version:
  {
    GST_DEBUG_OBJECT (h264parse, "wrong avcC version");
    goto refuse_caps;
  }
wrong_type:
  {
    GST_DEBUG_OBJECT (h264parse, "wrong codec-data type");
    goto refuse_caps;
  }
unknown_input_format:
  {
    GST_DEBUG_OBJECT (h264parse, "unknown stream-format and no codec_data");
    goto refuse_caps;
  }
refuse_caps:
  {
    GST_WARNING_OBJECT (h264parse, "refused caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstFlowReturn
gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstH264Parse *h264parse = GST_H264_PARSE (GST_PAD_PARENT (pad));

  if (h264parse->packetized && buffer) {
    GstByteReader br;
    GstBuffer *sub;
    GstFlowReturn ret = GST_FLOW_OK;
    guint32 len;
    const guint nl = h264parse->nal_length_size;

    GST_LOG_OBJECT (h264parse, "processing packet buffer of size %d",
        GST_BUFFER_SIZE (buffer));
    gst_byte_reader_init_from_buffer (&br, buffer);
    while (ret == GST_FLOW_OK && gst_byte_reader_get_remaining (&br)) {
      GST_DEBUG_OBJECT (h264parse, "AVC nal offset %d",
          gst_byte_reader_get_pos (&br));
      if (gst_byte_reader_get_remaining (&br) < nl)
        goto parse_failed;
      switch (nl) {
        case 4:
          len = gst_byte_reader_get_uint32_be_unchecked (&br);
          break;
        case 3:
          len = gst_byte_reader_get_uint24_be_unchecked (&br);
          break;
        case 2:
          len = gst_byte_reader_get_uint16_be_unchecked (&br);
          break;
        case 1:
          len = gst_byte_reader_get_uint8_unchecked (&br);
          break;
        default:
          goto not_negotiated;
          break;
      }
      GST_DEBUG_OBJECT (h264parse, "AVC nal size %d", len);
      if (gst_byte_reader_get_remaining (&br) < len)
        goto parse_failed;
      if (h264parse->split_packetized) {
        /* convert to NAL aligned byte stream input */
        sub = gst_h264_parse_wrap_nal (h264parse, GST_H264_PARSE_FORMAT_BYTE,
            (guint8 *) gst_byte_reader_get_data_unchecked (&br, len), len);
        /* at least this should make sense */
        GST_BUFFER_TIMESTAMP (sub) = GST_BUFFER_TIMESTAMP (buffer);
        GST_LOG_OBJECT (h264parse, "pushing NAL of size %d", len);
        ret = h264parse->parse_chain (pad, sub);
      } else {
        /* pass-through: no looking for frames (and nal processing),
         * so need to parse to collect data here */
        /* NOTE: so if it is really configured to do so,
         * pre_push can/will still insert codec-data at intervals,
         * which is not really pure pass-through, but anyway ... */
        gst_h264_parse_process_nal (h264parse,
            GST_BUFFER_DATA (buffer), gst_byte_reader_get_pos (&br) - nl,
            gst_byte_reader_get_pos (&br), len);
        gst_byte_reader_skip_unchecked (&br, len);
      }
    }
    if (h264parse->split_packetized)
      return ret;
    else {
      /* nal processing in pass-through might have collected stuff;
       * ensure nothing happens with this later on */
      gst_adapter_clear (h264parse->frame_out);
    }
  }

exit:
  return h264parse->parse_chain (pad, buffer);

  /* ERRORS */
not_negotiated:
  {
    GST_DEBUG_OBJECT (h264parse, "insufficient data to split input");
    return GST_FLOW_NOT_NEGOTIATED;
  }
parse_failed:
  {
    if (h264parse->split_packetized) {
      GST_ELEMENT_ERROR (h264parse, STREAM, FAILED, (NULL),
          ("invalid AVC input data"));
      return GST_FLOW_ERROR;
    } else {
      /* do not meddle to much in this case */
      GST_DEBUG_OBJECT (h264parse, "parsing packet failed");
      goto exit;
    }
  }
}

static void
gst_h264_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH264Parse *parse;

  parse = GST_H264_PARSE (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      parse->interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h264_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstH264Parse *parse;

  parse = GST_H264_PARSE (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      g_value_set_uint (value, parse->interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
