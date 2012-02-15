/* GStreamer H.264 Parser
 * Copyright (C) <2010> Collabora ltd
 * Copyright (C) <2010> Nokia Corporation
 * Copyright (C) <2011> Intel Corporation
 *
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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
#include <gst/video/video.h>
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
    GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, parsed = (boolean) true, "
        "stream-format=(string) { avc, byte-stream }, "
        "alignment=(string) { au, nal }"));

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
static GstCaps *gst_h264_parse_get_caps (GstBaseParse * parse);
static GstFlowReturn gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_h264_parse_event (GstBaseParse * parse, GstEvent * event);
static gboolean gst_h264_parse_src_event (GstBaseParse * parse,
    GstEvent * event);

static void
gst_h264_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_details_simple (gstelement_class, "H.264 parser",
      "Codec/Parser/Converter/Video",
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
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_h264_parse_get_caps);
  parse_class->event = GST_DEBUG_FUNCPTR (gst_h264_parse_event);
  parse_class->src_event = GST_DEBUG_FUNCPTR (gst_h264_parse_src_event);
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
  GST_DEBUG_OBJECT (h264parse, "reset frame");

  /* done parsing; reset state */
  h264parse->nalu.valid = FALSE;
  h264parse->nalu.offset = 0;
  h264parse->nalu.sc_offset = 0;
  h264parse->nalu.size = 0;
  h264parse->current_off = 0;

  h264parse->picture_start = FALSE;
  h264parse->update_caps = FALSE;
  h264parse->idr_pos = -1;
  h264parse->sei_pos = -1;
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
  h264parse->aspect_ratio_idc = 0;
  h264parse->sar_width = 0;
  h264parse->sar_height = 0;
  h264parse->upstream_par_n = -1;
  h264parse->upstream_par_d = -1;
  gst_buffer_replace (&h264parse->codec_data, NULL);
  h264parse->nal_length_size = 4;
  h264parse->packetized = FALSE;

  h264parse->align = GST_H264_PARSE_ALIGN_NONE;
  h264parse->format = GST_H264_PARSE_FORMAT_NONE;

  h264parse->last_report = GST_CLOCK_TIME_NONE;
  h264parse->push_codec = FALSE;

  h264parse->dts = GST_CLOCK_TIME_NONE;
  h264parse->ts_trn_nb = GST_CLOCK_TIME_NONE;
  h264parse->do_ts = TRUE;

  h264parse->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  h264parse->force_key_unit_event = NULL;

  gst_h264_parse_reset_frame (h264parse);
}

static gboolean
gst_h264_parse_start (GstBaseParse * parse)
{
  GstH264Parse *h264parse = GST_H264_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");
  gst_h264_parse_reset (h264parse);

  h264parse->nalparser = gst_h264_nal_parser_new ();

  h264parse->dts = GST_CLOCK_TIME_NONE;
  h264parse->ts_trn_nb = GST_CLOCK_TIME_NONE;
  h264parse->sei_pic_struct_pres_flag = FALSE;
  h264parse->sei_pic_struct = 0;
  h264parse->field_pic_flag = 0;

  gst_base_parse_set_min_frame_size (parse, 6);

  return TRUE;
}

static gboolean
gst_h264_parse_stop (GstBaseParse * parse)
{
  guint i;
  GstH264Parse *h264parse = GST_H264_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");
  gst_h264_parse_reset (h264parse);

  for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++)
    gst_buffer_replace (&h264parse->sps_nals[i], NULL);
  for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++)
    gst_buffer_replace (&h264parse->pps_nals[i], NULL);

  gst_h264_nal_parser_free (h264parse->nalparser);

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
  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG ("parsing caps: %" GST_PTR_FORMAT, caps);

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
gst_h264_parse_negotiate (GstH264Parse * h264parse, GstCaps * in_caps)
{
  GstCaps *caps;
  guint format = GST_H264_PARSE_FORMAT_NONE;
  guint align = GST_H264_PARSE_ALIGN_NONE;

  g_return_if_fail ((in_caps == NULL) || gst_caps_is_fixed (in_caps));

  caps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (h264parse));
  GST_DEBUG_OBJECT (h264parse, "allowed caps: %" GST_PTR_FORMAT, caps);

  /* concentrate on leading structure, since decodebin2 parser
   * capsfilter always includes parser template caps */
  if (caps) {
    caps = gst_caps_make_writable (caps);
    gst_caps_truncate (caps);
    GST_DEBUG_OBJECT (h264parse, "negotiating with caps: %" GST_PTR_FORMAT,
        caps);
  }

  if (in_caps && caps) {
    if (gst_caps_can_intersect (in_caps, caps)) {
      GST_DEBUG_OBJECT (h264parse, "downstream accepts upstream caps");
      gst_h264_parse_format_from_caps (in_caps, &format, &align);
      gst_caps_unref (caps);
      caps = NULL;
    }
  }

  if (caps) {
    /* fixate to avoid ambiguity with lists when parsing */
    gst_pad_fixate_caps (GST_BASE_PARSE_SRC_PAD (h264parse), caps);
    gst_h264_parse_format_from_caps (caps, &format, &align);
    gst_caps_unref (caps);
  }

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
  guint nl = h264parse->nal_length_size;

  GST_DEBUG_OBJECT (h264parse, "nal length %d", size);

  buf = gst_buffer_new_and_alloc (size + nl + 4);
  if (format == GST_H264_PARSE_FORMAT_AVC) {
    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), size << (32 - 8 * nl));
  } else {
    /* HACK: nl should always be 4 here, otherwise this won't work. 
     * There are legit cases where nl in avc stream is 2, but byte-stream
     * SC is still always 4 bytes. */
    nl = 4;
    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), 1);
  }

  GST_BUFFER_SIZE (buf) = size + nl;
  memcpy (GST_BUFFER_DATA (buf) + nl, data, size);

  return buf;
}

static void
gst_h264_parser_store_nal (GstH264Parse * h264parse, guint id,
    GstH264NalUnitType naltype, GstH264NalUnit * nalu)
{
  GstBuffer *buf, **store;
  guint size = nalu->size, store_size;

  if (naltype == GST_H264_NAL_SPS) {
    store_size = GST_H264_MAX_SPS_COUNT;
    store = h264parse->sps_nals;
    GST_DEBUG_OBJECT (h264parse, "storing sps %u", id);
  } else if (naltype == GST_H264_NAL_PPS) {
    store_size = GST_H264_MAX_PPS_COUNT;
    store = h264parse->pps_nals;
    GST_DEBUG_OBJECT (h264parse, "storing pps %u", id);
  } else
    return;

  if (id >= store_size) {
    GST_DEBUG_OBJECT (h264parse, "unable to store nal, id out-of-range %d", id);
    return;
  }

  buf = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buf), nalu->data + nalu->offset, size);

  if (store[id])
    gst_buffer_unref (store[id]);

  store[id] = buf;
}

/* SPS/PPS/IDR considered key, all others DELTA;
 * so downstream waiting for keyframe can pick up at SPS/PPS/IDR */
#define NAL_TYPE_IS_KEY(nt) (((nt) == 5) || ((nt) == 7) || ((nt) == 8))

/* caller guarantees 2 bytes of nal payload */
static void
gst_h264_parse_process_nal (GstH264Parse * h264parse, GstH264NalUnit * nalu)
{
  guint nal_type;
  GstH264PPS pps;
  GstH264SPS sps;
  GstH264SEIMessage sei;
  GstH264NalParser *nalparser = h264parse->nalparser;

  /* nothing to do for broken input */
  if (G_UNLIKELY (nalu->size < 2)) {
    GST_DEBUG_OBJECT (h264parse, "not processing nal size %u", nalu->size);
    return;
  }

  /* we have a peek as well */
  nal_type = nalu->type;
  h264parse->keyframe |= NAL_TYPE_IS_KEY (nal_type);

  GST_DEBUG_OBJECT (h264parse, "processing nal of type %u, size %u",
      nal_type, nalu->size);

  switch (nal_type) {
    case GST_H264_NAL_SPS:
      gst_h264_parser_parse_sps (nalparser, nalu, &sps, TRUE);

      GST_DEBUG_OBJECT (h264parse, "triggering src caps check");
      h264parse->update_caps = TRUE;
      /* found in stream, no need to forcibly push at start */
      h264parse->push_codec = FALSE;

      gst_h264_parser_store_nal (h264parse, sps.id, nal_type, nalu);
      break;
    case GST_H264_NAL_PPS:
      gst_h264_parser_parse_pps (nalparser, nalu, &pps);
      /* parameters might have changed, force caps check */
      GST_DEBUG_OBJECT (h264parse, "triggering src caps check");
      h264parse->update_caps = TRUE;
      /* found in stream, no need to forcibly push at start */
      h264parse->push_codec = FALSE;

      gst_h264_parser_store_nal (h264parse, pps.id, nal_type, nalu);
      break;
    case GST_H264_NAL_SEI:
      gst_h264_parser_parse_sei (nalparser, nalu, &sei);
      switch (sei.payloadType) {
        case GST_H264_SEI_PIC_TIMING:
          h264parse->sei_pic_struct_pres_flag =
              sei.pic_timing.pic_struct_present_flag;
          h264parse->sei_cpb_removal_delay = sei.pic_timing.cpb_removal_delay;
          if (h264parse->sei_pic_struct_pres_flag)
            h264parse->sei_pic_struct = sei.pic_timing.pic_struct;
          break;
        case GST_H264_SEI_BUF_PERIOD:
          if (h264parse->ts_trn_nb == GST_CLOCK_TIME_NONE ||
              h264parse->dts == GST_CLOCK_TIME_NONE)
            h264parse->ts_trn_nb = 0;
          else
            h264parse->ts_trn_nb = h264parse->dts;

          GST_LOG_OBJECT (h264parse,
              "new buffering period; ts_trn_nb updated: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (h264parse->ts_trn_nb));
          break;
      }
      /* mark SEI pos */
      if (h264parse->sei_pos == -1) {
        if (h264parse->format == GST_H264_PARSE_FORMAT_AVC)
          h264parse->sei_pos = gst_adapter_available (h264parse->frame_out);
        else
          h264parse->sei_pos = nalu->sc_offset;
        GST_DEBUG_OBJECT (h264parse, "marking SEI in frame at offset %d",
            h264parse->sei_pos);
      }
      break;

    case GST_H264_NAL_SLICE:
    case GST_H264_NAL_SLICE_DPA:
    case GST_H264_NAL_SLICE_DPB:
    case GST_H264_NAL_SLICE_DPC:
    case GST_H264_NAL_SLICE_IDR:
      /* don't need to parse the whole slice (header) here */
      if (*(nalu->data + nalu->offset + 1) & 0x80) {
        /* means first_mb_in_slice == 0 */
        /* real frame data */
        GST_DEBUG_OBJECT (h264parse, "first_mb_in_slice = 0");
        h264parse->frame_start = TRUE;
      }
      GST_DEBUG_OBJECT (h264parse, "frame start: %i", h264parse->frame_start);
#ifndef GST_DISABLE_GST_DEBUG
      {
        GstH264SliceHdr slice;
        GstH264ParserResult pres;

        pres = gst_h264_parser_parse_slice_hdr (nalparser, nalu, &slice,
            FALSE, FALSE);
        GST_DEBUG_OBJECT (h264parse,
            "parse result %d, first MB: %u, slice type: %u",
            pres, slice.first_mb_in_slice, slice.type);
      }
#endif
      if (G_LIKELY (nal_type != GST_H264_NAL_SLICE_IDR &&
              !h264parse->push_codec))
        break;
      /* if we need to sneak codec NALs into the stream,
       * this is a good place, so fake it as IDR
       * (which should be at start anyway) */
      /* mark where config needs to go if interval expired */
      /* mind replacement buffer if applicable */
      if (h264parse->idr_pos == -1) {
        if (h264parse->format == GST_H264_PARSE_FORMAT_AVC)
          h264parse->idr_pos = gst_adapter_available (h264parse->frame_out);
        else
          h264parse->idr_pos = nalu->sc_offset;
        GST_DEBUG_OBJECT (h264parse, "marking IDR in frame at offset %d",
            h264parse->idr_pos);
      }
      /* if SEI preceeds (faked) IDR, then we have to insert config there */
      if (h264parse->sei_pos >= 0 && h264parse->idr_pos > h264parse->sei_pos) {
        h264parse->idr_pos = h264parse->sei_pos;
        GST_DEBUG_OBJECT (h264parse, "moved IDR mark to SEI position %d",
            h264parse->idr_pos);
      }
      break;
    default:
      gst_h264_parser_parse_nal (nalparser, nalu);
  }

  /* if AVC output needed, collect properly prefixed nal in adapter,
   * and use that to replace outgoing buffer data later on */
  if (h264parse->format == GST_H264_PARSE_FORMAT_AVC) {
    GstBuffer *buf;

    GST_LOG_OBJECT (h264parse, "collecting NAL in AVC frame");
    buf = gst_h264_parse_wrap_nal (h264parse, h264parse->format,
        nalu->data + nalu->offset, nalu->size);
    gst_adapter_push (h264parse->frame_out, buf);
  }
}

/* caller guarantees at least 2 bytes of nal payload for each nal
 * returns TRUE if next_nal indicates that nal terminates an AU */
static inline gboolean
gst_h264_parse_collect_nal (GstH264Parse * h264parse, const guint8 * data,
    guint size, GstH264NalUnit * nalu)
{
  gboolean complete;
  GstH264ParserResult parse_res;
  GstH264NalUnitType nal_type = nalu->type;
  GstH264NalUnit nnalu;

  GST_DEBUG_OBJECT (h264parse, "parsing collected nal");
  parse_res = gst_h264_parser_identify_nalu (h264parse->nalparser, data,
      nalu->offset + nalu->size, size, &nnalu);

  if (parse_res == GST_H264_PARSER_ERROR)
    return FALSE;

  /* determine if AU complete */
  GST_LOG_OBJECT (h264parse, "nal type: %d", nal_type);
  /* coded slice NAL starts a picture,
   * i.e. other types become aggregated in front of it */
  h264parse->picture_start |= (nal_type == GST_H264_NAL_SLICE ||
      nal_type == GST_H264_NAL_SLICE_DPA || nal_type == GST_H264_NAL_SLICE_IDR);

  /* consider a coded slices (IDR or not) to start a picture,
   * (so ending the previous one) if first_mb_in_slice == 0
   * (non-0 is part of previous one) */
  /* NOTE this is not entirely according to Access Unit specs in 7.4.1.2.4,
   * but in practice it works in sane cases, needs not much parsing,
   * and also works with broken frame_num in NAL
   * (where spec-wise would fail) */
  nal_type = nnalu.type;
  complete = h264parse->picture_start && (nal_type >= GST_H264_NAL_SEI &&
      nal_type <= GST_H264_NAL_AU_DELIMITER);

  GST_LOG_OBJECT (h264parse, "next nal type: %d", nal_type);
  complete |= h264parse->picture_start &&
      (nal_type == GST_H264_NAL_SLICE ||
      nal_type == GST_H264_NAL_SLICE_DPA ||
      nal_type == GST_H264_NAL_SLICE_IDR) &&
      /* first_mb_in_slice == 0 considered start of frame */
      (nnalu.data[nnalu.offset + 1] & 0x80);

  GST_LOG_OBJECT (h264parse, "au complete: %d", complete);

  return complete;
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
  guint8 *data;
  guint size, current_off = 0;
  gboolean drain;
  GstH264NalParser *nalparser = h264parse->nalparser;
  GstH264NalUnit nalu;

  /* expect at least 3 bytes startcode == sc, and 2 bytes NALU payload */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buffer) < 5))
    return FALSE;

  /* need to configure aggregation */
  if (G_UNLIKELY (h264parse->format == GST_H264_PARSE_FORMAT_NONE))
    gst_h264_parse_negotiate (h264parse, NULL);

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

  drain = FALSE;
  nalu = h264parse->nalu;
  current_off = h264parse->current_off;

  g_assert (current_off < size);

  GST_DEBUG_OBJECT (h264parse, "last parse position %u", current_off);
  while (TRUE) {
    GstH264ParserResult pres;

    if (h264parse->packetized_chunked)
      pres =
          gst_h264_parser_identify_nalu_unchecked (nalparser, data, current_off,
          size, &nalu);
    else
      pres =
          gst_h264_parser_identify_nalu (nalparser, data, current_off, size,
          &nalu);

    switch (pres) {
      case GST_H264_PARSER_OK:
        GST_DEBUG_OBJECT (h264parse, "complete nal found. "
            "current offset: %u, Nal offset: %u, Nal Size: %u",
            current_off, nalu.offset, nalu.size);

        GST_DEBUG_OBJECT (h264parse, "current off. %u",
            nalu.offset + nalu.size);

        if (!h264parse->nalu.size && !h264parse->nalu.valid)
          h264parse->nalu = nalu;

        /* need 2 bytes of next nal */
        if (!h264parse->packetized_chunked &&
            (nalu.offset + nalu.size + 4 + 2 > size)) {
          if (GST_BASE_PARSE_DRAINING (parse)) {
            drain = TRUE;
          } else {
            GST_DEBUG_OBJECT (h264parse, "need more bytes of next nal");
            current_off = nalu.sc_offset;
            goto more;
          }
        } else if (h264parse->packetized_chunked) {
          /* normal next nal based collection not possible,
           * _chain will have to tell us whether this was last one for AU */
          drain = h264parse->packetized_last;
        }
        break;
      case GST_H264_PARSER_BROKEN_LINK:
        return FALSE;
      case GST_H264_PARSER_ERROR:
        current_off = size - 3;
        goto parsing_error;
      case GST_H264_PARSER_NO_NAL:
        /* don't expect to have found any NAL so far */
        g_assert (h264parse->nalu.size == 0);
        current_off = h264parse->nalu.sc_offset = size - 3;
        goto more;
      case GST_H264_PARSER_BROKEN_DATA:
        GST_WARNING_OBJECT (h264parse, "input stream is corrupt; "
            "it contains a NAL unit of length %d", nalu.size);

        /* broken nal at start -> arrange to skip it,
         * otherwise have it terminate current au
         * (and so it will be skipped on next frame round) */
        if (nalu.sc_offset == h264parse->nalu.sc_offset) {
          *skipsize = nalu.offset;

          GST_DEBUG_OBJECT (h264parse, "skipping broken nal");
          goto invalid;
        } else {
          nalu.size = 0;
          goto end;
        }
      case GST_H264_PARSER_NO_NAL_END:
        GST_DEBUG_OBJECT (h264parse, "not a complete nal found at offset %u",
            nalu.offset);

        current_off = nalu.sc_offset;
        /* We keep the reference to this nal so we start over the parsing
         * here */
        if (!h264parse->nalu.size && !h264parse->nalu.valid)
          h264parse->nalu = nalu;

        if (GST_BASE_PARSE_DRAINING (parse)) {
          drain = TRUE;
          GST_DEBUG_OBJECT (h264parse, "draining NAL %u %u %u", size,
              h264parse->nalu.offset, h264parse->nalu.size);
          /*  Can't parse the nalu */
          if (size - h264parse->nalu.offset < 2) {
            *skipsize = nalu.offset;
            goto invalid;
          }

          /* We parse it anyway */
          nalu.size = size - nalu.offset;
          break;
        }
        goto more;
    }

    current_off = nalu.offset + nalu.size;

    GST_DEBUG_OBJECT (h264parse, "%p complete nal found. Off: %u, Size: %u",
        data, nalu.offset, nalu.size);

    gst_h264_parse_process_nal (h264parse, &nalu);

    /* simulate no next nal if none needed */
    drain = drain || (h264parse->align == GST_H264_PARSE_ALIGN_NAL);

    /* In packetized mode we know there's only on NALU in each input packet,
     * but we may not have seen the whole AU already, possibly need more */
    if (h264parse->packetized_chunked) {
      if (drain)
        break;
      /* next NALU expected at end of current data */
      current_off = size;
      goto more;
    }

    /* if no next nal, we know it's complete here */
    if (drain || gst_h264_parse_collect_nal (h264parse, data, size, &nalu))
      break;

    GST_DEBUG_OBJECT (h264parse, "Looking for more");
  }

end:
  *skipsize = h264parse->nalu.sc_offset;
  *framesize = nalu.offset + nalu.size - h264parse->nalu.sc_offset;
  h264parse->current_off = current_off;

  return TRUE;

parsing_error:
  GST_DEBUG_OBJECT (h264parse, "error parsing Nal Unit");

more:
  /* ask for best next available */
  *framesize = G_MAXUINT;
  if (!h264parse->nalu.size) {
    /* skip up to initial startcode */
    *skipsize = h264parse->nalu.sc_offset;
    /* but mind some stuff will have been skipped */
    g_assert (current_off >= *skipsize);
    current_off -= *skipsize;
    h264parse->nalu.sc_offset = 0;
  } else {
    *skipsize = 0;
  }

  /* Restart parsing from here next time */
  h264parse->current_off = current_off;

  return FALSE;

invalid:
  gst_h264_parse_reset_frame (h264parse);
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

  for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++) {
    if ((nal = h264parse->sps_nals[i])) {
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
  for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++) {
    if ((nal = h264parse->pps_nals[i])) {
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
  for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++) {
    if ((nal = h264parse->sps_nals[i])) {
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (nal));
      memcpy (data + 2, GST_BUFFER_DATA (nal), GST_BUFFER_SIZE (nal));
      data += 2 + GST_BUFFER_SIZE (nal);
    }
  }

  data[0] = num_pps;
  data++;
  for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++) {
    if ((nal = h264parse->pps_nals[i])) {
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (nal));
      memcpy (data + 2, GST_BUFFER_DATA (nal), GST_BUFFER_SIZE (nal));
      data += 2 + GST_BUFFER_SIZE (nal);
    }
  }

  return buf;
}

static void
gst_h264_parse_get_par (GstH264Parse * h264parse, gint * num, gint * den)
{
  gint par_n, par_d;

  if (h264parse->upstream_par_n != -1 && h264parse->upstream_par_d != -1) {
    *num = h264parse->upstream_par_n;
    *den = h264parse->upstream_par_d;
    return;
  }

  par_n = par_d = 0;
  switch (h264parse->aspect_ratio_idc) {
    case 0:
      par_n = par_d = 0;
      break;
    case 1:
      par_n = 1;
      par_d = 1;
      break;
    case 2:
      par_n = 12;
      par_d = 11;
      break;
    case 3:
      par_n = 10;
      par_d = 11;
      break;
    case 4:
      par_n = 16;
      par_d = 11;
      break;
    case 5:
      par_n = 40;
      par_d = 33;
      break;
    case 6:
      par_n = 24;
      par_d = 11;
      break;
    case 7:
      par_n = 20;
      par_d = 11;
      break;
    case 8:
      par_n = 32;
      par_d = 11;
      break;
    case 9:
      par_n = 80;
      par_d = 33;
      break;
    case 10:
      par_n = 18;
      par_d = 11;
      break;
    case 11:
      par_n = 15;
      par_d = 11;
      break;
    case 12:
      par_n = 64;
      par_d = 33;
      break;
    case 13:
      par_n = 160;
      par_d = 99;
      break;
    case 14:
      par_n = 4;
      par_d = 3;
      break;
    case 15:
      par_n = 3;
      par_d = 2;
      break;
    case 16:
      par_n = 2;
      par_d = 1;
      break;
    case 255:
      par_n = h264parse->sar_width;
      par_d = h264parse->sar_height;
      break;
    default:
      par_n = par_d = 0;
  }

  *num = par_n;
  *den = par_d;
}

static void
gst_h264_parse_update_src_caps (GstH264Parse * h264parse, GstCaps * caps)
{
  GstH264SPS *sps;
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

  sps = h264parse->nalparser->last_sps;
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
  } else {
    if (G_UNLIKELY (h264parse->width != sps->width ||
            h264parse->height != sps->height)) {
      GST_INFO_OBJECT (h264parse, "resolution changed %dx%d",
          sps->width, sps->height);
      h264parse->width = sps->width;
      h264parse->height = sps->height;
      modified = TRUE;
    }

    /* 0/1 is set as the default in the codec parser */
    if (sps->vui_parameters.timing_info_present_flag &&
        !(sps->fps_num == 0 && sps->fps_den == 1)) {
      if (G_UNLIKELY (h264parse->fps_num != sps->fps_num
              || h264parse->fps_den != sps->fps_den)) {
        GST_INFO_OBJECT (h264parse, "framerate changed %d/%d",
            sps->fps_num, sps->fps_den);
        h264parse->fps_num = sps->fps_num;
        h264parse->fps_den = sps->fps_den;
        gst_base_parse_set_frame_rate (GST_BASE_PARSE (h264parse),
            h264parse->fps_num, h264parse->fps_den, 0, 0);
        modified = TRUE;
      }
    }

    if (sps->vui_parameters.aspect_ratio_info_present_flag) {
      if (G_UNLIKELY (h264parse->aspect_ratio_idc !=
              sps->vui_parameters.aspect_ratio_idc)) {
        h264parse->aspect_ratio_idc = sps->vui_parameters.aspect_ratio_idc;
        GST_INFO_OBJECT (h264parse, "aspect ratio idc changed %d",
            h264parse->aspect_ratio_idc);
        modified = TRUE;
      }

      /* 255 means sar_width and sar_height present */
      if (G_UNLIKELY (sps->vui_parameters.aspect_ratio_idc == 255 &&
              (h264parse->sar_width != sps->vui_parameters.sar_width ||
                  h264parse->sar_height != sps->vui_parameters.sar_height))) {
        h264parse->sar_width = sps->vui_parameters.sar_width;
        h264parse->sar_height = sps->vui_parameters.sar_height;
        GST_INFO_OBJECT (h264parse, "aspect ratio SAR changed %d/%d",
            h264parse->sar_width, h264parse->sar_height);
        modified = TRUE;
      }
    }

    if (G_UNLIKELY (modified)) {
      caps = gst_caps_copy (sink_caps);
      /* sps should give this */
      gst_caps_set_simple (caps, "width", G_TYPE_INT, sps->width,
          "height", G_TYPE_INT, sps->height, NULL);
      /* but not necessarily or reliably this */
      if (h264parse->fps_num > 0 && h264parse->fps_den > 0)
        gst_caps_set_simple (caps, "framerate",
            GST_TYPE_FRACTION, h264parse->fps_num, h264parse->fps_den, NULL);
    }
  }

  if (caps) {
    gint par_n, par_d;

    gst_caps_set_simple (caps, "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h264_parse_get_string (h264parse, TRUE, h264parse->format),
        "alignment", G_TYPE_STRING,
        gst_h264_parse_get_string (h264parse, FALSE, h264parse->align), NULL);

    gst_h264_parse_get_par (h264parse, &par_n, &par_d);
    if (par_n != 0 && par_d != 0) {
      GST_INFO_OBJECT (h264parse, "PAR %d/%d", par_n, par_d);
      gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          par_n, par_d, NULL);
    }

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

static void
gst_h264_parse_get_timestamp (GstH264Parse * h264parse,
    GstClockTime * out_ts, GstClockTime * out_dur, gboolean frame)
{
  GstH264SPS *sps = h264parse->nalparser->last_sps;
  GstClockTime upstream;
  gint duration = 1;

  g_return_if_fail (out_dur != NULL);
  g_return_if_fail (out_ts != NULL);

  upstream = *out_ts;

  if (!frame) {
    GST_LOG_OBJECT (h264parse, "no frame data ->  0 duration");
    *out_dur = 0;
    goto exit;
  } else {
    *out_ts = upstream;
  }

  if (!sps) {
    GST_DEBUG_OBJECT (h264parse, "referred SPS invalid");
    goto exit;
  } else if (!sps->vui_parameters.timing_info_present_flag) {
    GST_DEBUG_OBJECT (h264parse,
        "unable to compute timestamp: timing info not present");
    goto exit;
  } else if (sps->vui_parameters.time_scale == 0) {
    GST_DEBUG_OBJECT (h264parse,
        "unable to compute timestamp: time_scale = 0 "
        "(this is forbidden in spec; bitstream probably contains error)");
    goto exit;
  }

  if (h264parse->sei_pic_struct_pres_flag &&
      h264parse->sei_pic_struct != (guint8) - 1) {
    /* Note that when h264parse->sei_pic_struct == -1 (unspecified), there
     * are ways to infer its value. This is related to computing the
     * TopFieldOrderCnt and BottomFieldOrderCnt, which looks
     * complicated and thus not implemented for the time being. Yet
     * the value we have here is correct for many applications
     */
    switch (h264parse->sei_pic_struct) {
      case GST_H264_SEI_PIC_STRUCT_TOP_FIELD:
      case GST_H264_SEI_PIC_STRUCT_BOTTOM_FIELD:
        duration = 1;
        break;
      case GST_H264_SEI_PIC_STRUCT_FRAME:
      case GST_H264_SEI_PIC_STRUCT_TOP_BOTTOM:
      case GST_H264_SEI_PIC_STRUCT_BOTTOM_TOP:
        duration = 2;
        break;
      case GST_H264_SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
      case GST_H264_SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
        duration = 3;
        break;
      case GST_H264_SEI_PIC_STRUCT_FRAME_DOUBLING:
        duration = 4;
        break;
      case GST_H264_SEI_PIC_STRUCT_FRAME_TRIPLING:
        duration = 6;
        break;
      default:
        GST_DEBUG_OBJECT (h264parse,
            "h264parse->sei_pic_struct of unknown value %d. Not parsed",
            h264parse->sei_pic_struct);
        break;
    }
  } else {
    duration = h264parse->field_pic_flag ? 1 : 2;
  }

  GST_LOG_OBJECT (h264parse, "frame tick duration %d", duration);

  /*
   * h264parse.264 C.1.2 Timing of coded picture removal (equivalent to DTS):
   * Tr,n(0) = initial_cpb_removal_delay[ SchedSelIdx ] / 90000
   * Tr,n(n) = Tr,n(nb) + Tc * cpb_removal_delay(n)
   * where
   * Tc = num_units_in_tick / time_scale
   */

  if (h264parse->ts_trn_nb != GST_CLOCK_TIME_NONE) {
    GST_LOG_OBJECT (h264parse, "buffering based ts");
    /* buffering period is present */
    if (upstream != GST_CLOCK_TIME_NONE) {
      /* If upstream timestamp is valid, we respect it and adjust current
       * reference point */
      h264parse->ts_trn_nb = upstream -
          (GstClockTime) gst_util_uint64_scale_int
          (h264parse->sei_cpb_removal_delay * GST_SECOND,
          sps->vui_parameters.num_units_in_tick,
          sps->vui_parameters.time_scale);
    } else {
      /* If no upstream timestamp is given, we write in new timestamp */
      upstream = h264parse->dts = h264parse->ts_trn_nb +
          (GstClockTime) gst_util_uint64_scale_int
          (h264parse->sei_cpb_removal_delay * GST_SECOND,
          sps->vui_parameters.num_units_in_tick,
          sps->vui_parameters.time_scale);
    }
  } else {
    GstClockTime dur;

    GST_LOG_OBJECT (h264parse, "duration based ts");
    /* naive method: no removal delay specified
     * track upstream timestamp and provide best guess frame duration */
    dur = gst_util_uint64_scale_int (duration * GST_SECOND,
        sps->vui_parameters.num_units_in_tick, sps->vui_parameters.time_scale);
    /* sanity check */
    if (dur < GST_MSECOND) {
      GST_DEBUG_OBJECT (h264parse, "discarding dur %" GST_TIME_FORMAT,
          GST_TIME_ARGS (dur));
    } else {
      *out_dur = dur;
    }
  }

exit:
  if (GST_CLOCK_TIME_IS_VALID (upstream))
    *out_ts = h264parse->dts = upstream;

  if (GST_CLOCK_TIME_IS_VALID (*out_dur) &&
      GST_CLOCK_TIME_IS_VALID (h264parse->dts))
    h264parse->dts += *out_dur;
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

  /* don't mess with timestamps if provided by upstream,
   * particularly since our ts not that good they handle seeking etc */
  if (h264parse->do_ts)
    gst_h264_parse_get_timestamp (h264parse,
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

static GstEvent *
check_pending_key_unit_event (GstEvent * pending_event, GstSegment * segment,
    GstClockTime timestamp, guint flags, GstClockTime pending_key_unit_ts)
{
  GstClockTime running_time, stream_time;
  gboolean all_headers;
  guint count;
  GstEvent *event = NULL;

  g_return_val_if_fail (segment != NULL, NULL);

  if (pending_event == NULL)
    goto out;

  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      timestamp == GST_CLOCK_TIME_NONE)
    goto out;

  running_time = gst_segment_to_running_time (segment,
      GST_FORMAT_TIME, timestamp);

  GST_INFO ("now %" GST_TIME_FORMAT " wanted %" GST_TIME_FORMAT,
      GST_TIME_ARGS (running_time), GST_TIME_ARGS (pending_key_unit_ts));
  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      running_time < pending_key_unit_ts)
    goto out;

#if 0
  if (flags & GST_BUFFER_FLAG_DELTA_UNIT) {
    GST_DEBUG ("pending force key unit, waiting for keyframe");
    goto out;
  }
#endif

  stream_time = gst_segment_to_stream_time (segment,
      GST_FORMAT_TIME, timestamp);

  gst_video_event_parse_upstream_force_key_unit (pending_event,
      NULL, &all_headers, &count);

  event =
      gst_video_event_new_downstream_force_key_unit (timestamp, stream_time,
      running_time, all_headers, count);
  gst_event_set_seqnum (event, gst_event_get_seqnum (pending_event));

out:
  return event;
}

static void
gst_h264_parse_prepare_key_unit (GstH264Parse * parse, GstEvent * event)
{
  GstClockTime running_time;
  guint count;
  gboolean have_sps, have_pps;
  gint i;

  parse->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  gst_event_replace (&parse->force_key_unit_event, NULL);

  gst_video_event_parse_downstream_force_key_unit (event,
      NULL, NULL, &running_time, NULL, &count);

  GST_INFO_OBJECT (parse, "pushing downstream force-key-unit event %d "
      "%" GST_TIME_FORMAT " count %d", gst_event_get_seqnum (event),
      GST_TIME_ARGS (running_time), count);
  gst_pad_push_event (GST_BASE_PARSE_SRC_PAD (parse), event);

  have_sps = have_pps = FALSE;
  for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++) {
    if (parse->sps_nals[i] != NULL) {
      have_sps = TRUE;
      break;
    }
  }
  for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++) {
    if (parse->pps_nals[i] != NULL) {
      have_pps = TRUE;
      break;
    }
  }

  GST_INFO_OBJECT (parse, "preparing key unit, have sps %d have pps %d",
      have_sps, have_pps);

  /* set push_codec to TRUE so that pre_push_frame sends SPS/PPS again */
  parse->push_codec = TRUE;
}

static GstFlowReturn
gst_h264_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstH264Parse *h264parse;
  GstBuffer *buffer;
  GstEvent *event;

  h264parse = GST_H264_PARSE (parse);
  buffer = frame->buffer;

  if ((event = check_pending_key_unit_event (h264parse->force_key_unit_event,
              &parse->segment, GST_BUFFER_TIMESTAMP (buffer),
              GST_BUFFER_FLAGS (buffer), h264parse->pending_key_unit_ts))) {
    gst_h264_parse_prepare_key_unit (h264parse, event);
  }

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
          for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++) {
            if ((codec_nal = h264parse->sps_nals[i])) {
              GST_DEBUG_OBJECT (h264parse, "sending SPS nal");
              gst_h264_parse_push_codec_buffer (h264parse, codec_nal,
                  timestamp);
              h264parse->last_report = new_ts;
            }
          }
          for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++) {
            if ((codec_nal = h264parse->pps_nals[i])) {
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
          for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++) {
            if ((codec_nal = h264parse->sps_nals[i])) {
              GST_DEBUG_OBJECT (h264parse, "inserting SPS nal");
              gst_byte_writer_put_uint32_be (&bw,
                  bs ? 1 : GST_BUFFER_SIZE (codec_nal));
              gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (codec_nal),
                  GST_BUFFER_SIZE (codec_nal));
              h264parse->last_report = new_ts;
            }
          }
          for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++) {
            if ((codec_nal = h264parse->pps_nals[i])) {
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
          /* should already be keyframe/IDR, but it may not have been,
           * so mark it as such to avoid being discarded by picky decoder */
          GST_BUFFER_FLAG_UNSET (new_buf, GST_BUFFER_FLAG_DELTA_UNIT);
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
  guint size, format, align, off;
  GstH264NalUnit nalu;
  GstH264ParserResult parseres;

  h264parse = GST_H264_PARSE (parse);

  /* reset */
  h264parse->push_codec = FALSE;

  str = gst_caps_get_structure (caps, 0);

  /* accept upstream info if provided */
  gst_structure_get_int (str, "width", &h264parse->width);
  gst_structure_get_int (str, "height", &h264parse->height);
  gst_structure_get_fraction (str, "framerate", &h264parse->fps_num,
      &h264parse->fps_den);
  gst_structure_get_fraction (str, "pixel-aspect-ratio",
      &h264parse->upstream_par_n, &h264parse->upstream_par_d);

  /* get upstream format and align from caps */
  gst_h264_parse_format_from_caps (caps, &format, &align);

  /* packetized video has a codec_data */
  if (format != GST_H264_PARSE_FORMAT_BYTE &&
      (value = gst_structure_get_value (str, "codec_data"))) {
    guint8 *data;
    guint num_sps, num_pps, profile;
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
    if (size < 8)
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
    GST_DEBUG_OBJECT (h264parse, "nal length size %u",
        h264parse->nal_length_size);

    num_sps = data[5] & 0x1f;
    off = 6;
    for (i = 0; i < num_sps; i++) {
      parseres = gst_h264_parser_identify_nalu_avc (h264parse->nalparser,
          data, off, size, 2, &nalu);
      if (parseres != GST_H264_PARSER_OK)
        goto avcc_too_small;

      gst_h264_parse_process_nal (h264parse, &nalu);
      off = nalu.offset + nalu.size;
    }

    num_pps = data[off];
    off++;

    for (i = 0; i < num_pps; i++) {
      parseres = gst_h264_parser_identify_nalu_avc (h264parse->nalparser,
          data, off, size, 2, &nalu);
      if (parseres != GST_H264_PARSER_OK) {
        goto avcc_too_small;
      }

      gst_h264_parse_process_nal (h264parse, &nalu);
      off = nalu.offset + nalu.size;
    }

    h264parse->codec_data = gst_buffer_ref (codec_data);

    /* if upstream sets codec_data without setting stream-format and alignment, we
     * assume stream-format=avc,alignment=au */
    if (format == GST_H264_PARSE_FORMAT_NONE) {
      format = GST_H264_PARSE_FORMAT_AVC;
      align = GST_H264_PARSE_ALIGN_AU;
    }
  } else {
    GST_DEBUG_OBJECT (h264parse, "have bytestream h264");
    /* nothing to pre-process */
    h264parse->packetized = FALSE;
    /* we have 4 sync bytes */
    h264parse->nal_length_size = 4;

    if (format == GST_H264_PARSE_FORMAT_NONE) {
      format = GST_H264_PARSE_FORMAT_BYTE;
      align = GST_H264_PARSE_ALIGN_AU;
    }
  }

  {
    GstCaps *in_caps;

    /* prefer input type determined above */
    in_caps = gst_caps_new_simple ("video/x-h264",
        "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h264_parse_get_string (h264parse, TRUE, format),
        "alignment", G_TYPE_STRING,
        gst_h264_parse_get_string (h264parse, FALSE, align), NULL);
    /* negotiate with downstream, sets ->format and ->align */
    gst_h264_parse_negotiate (h264parse, in_caps);
    gst_caps_unref (in_caps);
  }

  if (format == h264parse->format && align == h264parse->align) {
    gst_base_parse_set_passthrough (parse, TRUE);

    /* we did parse codec-data and might supplement src caps */
    gst_h264_parse_update_src_caps (h264parse, caps);
  } else if (format == GST_H264_PARSE_FORMAT_AVC) {
    /* if input != output, and input is avc, must split before anything else */
    /* arrange to insert codec-data in-stream if needed.
     * src caps are only arranged for later on */
    h264parse->push_codec = TRUE;
    h264parse->split_packetized = TRUE;
    h264parse->packetized = TRUE;
  }

  return TRUE;

  /* ERRORS */
avcc_too_small:
  {
    GST_DEBUG_OBJECT (h264parse, "avcC size %u < 8", size);
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
refuse_caps:
  {
    GST_WARNING_OBJECT (h264parse, "refused caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstCaps *
gst_h264_parse_get_caps (GstBaseParse * parse)
{
  GstCaps *peercaps;
  GstCaps *res;

  peercaps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (parse));
  if (peercaps) {
    guint i, n;

    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);
      gst_structure_remove_field (s, "alignment");
      gst_structure_remove_field (s, "stream-format");
      gst_structure_remove_field (s, "parsed");
    }

    res =
        gst_caps_intersect_full (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse)),
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
  } else {
    res =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD
            (parse)));
  }

  return res;
}

static gboolean
gst_h264_parse_event (GstBaseParse * parse, GstEvent * event)
{
  gboolean handled = FALSE;
  GstH264Parse *h264parse = GST_H264_PARSE (parse);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstClockTime timestamp, stream_time, running_time;
      gboolean all_headers;
      guint count;

      if (!gst_video_event_is_force_key_unit (event))
        break;

      gst_video_event_parse_downstream_force_key_unit (event,
          &timestamp, &stream_time, &running_time, &all_headers, &count);

      GST_INFO_OBJECT (h264parse, "received downstream force key unit event, "
          "seqnum %d running_time %" GST_TIME_FORMAT " all_headers %d count %d",
          gst_event_get_seqnum (event), GST_TIME_ARGS (running_time),
          all_headers, count);
      handled = TRUE;

      if (h264parse->force_key_unit_event) {
        GST_INFO_OBJECT (h264parse, "ignoring force key unit event "
            "as one is already queued");
        break;
      }

      h264parse->pending_key_unit_ts = running_time;
      gst_event_replace (&h264parse->force_key_unit_event, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      h264parse->dts = GST_CLOCK_TIME_NONE;
      h264parse->ts_trn_nb = GST_CLOCK_TIME_NONE;
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start;

      gst_event_parse_new_segment_full (event, NULL, &rate, &applied_rate,
          &format, &start, NULL, NULL);
      /* don't try to mess with more subtle cases (e.g. seek) */
      if (format == GST_FORMAT_TIME &&
          (start != 0 || rate != 1.0 || applied_rate != 1.0))
        h264parse->do_ts = FALSE;
      break;
    }
    default:
      break;
  }

  return handled;
}

static gboolean
gst_h264_parse_src_event (GstBaseParse * parse, GstEvent * event)
{
  gboolean handled = FALSE;
  GstH264Parse *h264parse = GST_H264_PARSE (parse);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      GstClockTime running_time;
      gboolean all_headers;
      guint count;

      if (!gst_video_event_is_force_key_unit (event))
        break;

      gst_video_event_parse_upstream_force_key_unit (event,
          &running_time, &all_headers, &count);

      GST_INFO_OBJECT (h264parse, "received upstream force-key-unit event, "
          "seqnum %d running_time %" GST_TIME_FORMAT " all_headers %d count %d",
          gst_event_get_seqnum (event), GST_TIME_ARGS (running_time),
          all_headers, count);

      if (!all_headers)
        break;

      h264parse->pending_key_unit_ts = running_time;
      gst_event_replace (&h264parse->force_key_unit_event, event);
      /* leave handled = FALSE so that the event gets propagated upstream */
      break;
    }
    default:
      break;
  }

  return handled;
}

static GstFlowReturn
gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstH264Parse *h264parse = GST_H264_PARSE (GST_PAD_PARENT (pad));

  if (h264parse->packetized && buffer) {
    GstBuffer *sub;
    GstFlowReturn ret = GST_FLOW_OK;
    GstH264ParserResult parse_res;
    GstH264NalUnit nalu;
    const guint nl = h264parse->nal_length_size;

    if (nl < 1 || nl > 4) {
      GST_DEBUG_OBJECT (h264parse, "insufficient data to split input");
      gst_buffer_unref (buffer);

      return GST_FLOW_NOT_NEGOTIATED;
    }

    GST_LOG_OBJECT (h264parse, "processing packet buffer of size %d",
        GST_BUFFER_SIZE (buffer));

    parse_res = gst_h264_parser_identify_nalu_avc (h264parse->nalparser,
        GST_BUFFER_DATA (buffer), 0, GST_BUFFER_SIZE (buffer), nl, &nalu);

    while (parse_res == GST_H264_PARSER_OK) {
      GST_DEBUG_OBJECT (h264parse, "AVC nal offset %d",
          nalu.offset + nalu.size);

      if (h264parse->split_packetized) {
        /* convert to NAL aligned byte stream input */
        sub = gst_h264_parse_wrap_nal (h264parse, GST_H264_PARSE_FORMAT_BYTE,
            nalu.data + nalu.offset, nalu.size);
        /* at least this should make sense */
        GST_BUFFER_TIMESTAMP (sub) = GST_BUFFER_TIMESTAMP (buffer);
        /* transfer flags (e.g. DISCONT) for first fragment */
        if (nalu.offset <= nl)
          gst_buffer_copy_metadata (sub, buffer, GST_BUFFER_COPY_FLAGS);
        /* in reverse playback, baseparse gathers buffers, so we cannot
         * guarantee a buffer to contain a single whole NALU */
        h264parse->packetized_chunked =
            (GST_BASE_PARSE (h264parse)->segment.rate > 0.0);
        h264parse->packetized_last =
            (nalu.offset + nalu.size + nl >= GST_BUFFER_SIZE (buffer));
        GST_LOG_OBJECT (h264parse, "pushing NAL of size %d, last = %d",
            nalu.size, h264parse->packetized_last);
        ret = h264parse->parse_chain (pad, sub);
      } else {
        /* pass-through: no looking for frames (and nal processing),
         * so need to parse to collect data here */
        /* NOTE: so if it is really configured to do so,
         * pre_push can/will still insert codec-data at intervals,
         * which is not really pure pass-through, but anyway ... */
        gst_h264_parse_process_nal (h264parse, &nalu);

      }

      parse_res = gst_h264_parser_identify_nalu_avc (h264parse->nalparser,
          GST_BUFFER_DATA (buffer), nalu.offset + nalu.size,
          GST_BUFFER_SIZE (buffer), nl, &nalu);
    }

    if (h264parse->split_packetized) {
      gst_buffer_unref (buffer);
      return ret;
    } else {
      /* nal processing in pass-through might have collected stuff;
       * ensure nothing happens with this later on */
      gst_adapter_clear (h264parse->frame_out);
    }

    if (parse_res == GST_H264_PARSER_NO_NAL_END ||
        parse_res == GST_H264_PARSER_BROKEN_DATA) {

      if (h264parse->split_packetized) {
        GST_ELEMENT_ERROR (h264parse, STREAM, FAILED, (NULL),
            ("invalid AVC input data"));
        gst_buffer_unref (buffer);

        return GST_FLOW_ERROR;
      } else {
        /* do not meddle to much in this case */
        GST_DEBUG_OBJECT (h264parse, "parsing packet failed");
      }
    }
  }

  return h264parse->parse_chain (pad, buffer);
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
