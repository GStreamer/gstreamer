/* GStreamer
 * Copyright (C) 2023 He Junyan <junyan.he@intel.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:gsth266decoder
 * @title: GstH266Decoder
 * @short_description: Base class to implement stateless H.266 decoders
 * @sources:
 * - gsth266picture.h
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/base/base.h>
#include "gsth266decoder.h"

GST_DEBUG_CATEGORY (gst_h266_decoder_debug);
#define GST_CAT_DEFAULT gst_h266_decoder_debug

typedef enum
{
  GST_H266_DECODER_FORMAT_NONE,
  GST_H266_DECODER_FORMAT_VVC1,
  GST_H266_DECODER_FORMAT_VVI1,
  GST_H266_DECODER_FORMAT_BYTE
} GstH266DecoderFormat;

typedef enum
{
  GST_H266_DECODER_ALIGN_NONE,
  GST_H266_DECODER_ALIGN_NAL,
  GST_H266_DECODER_ALIGN_AU
} GstH266DecoderAlign;

struct _GstH266DecoderPrivate
{
  /* state */
  gint max_width, max_height;
  guint8 conformance_window_flag;
  gint crop_rect_width;
  gint crop_rect_height;
  gint crop_rect_x;
  gint crop_rect_y;

  GstH266DecoderFormat in_format;
  GstH266DecoderAlign align;
  guint nal_length_size;

  GstH266Parser *parser;
  GstH266Dpb *dpb;

  /* 0: frame or field-pair interlaced stream
   * 1: alternating, single field interlaced stream.
   * When equal to 1, picture timing SEI shall be present in every AU */
  guint8 field_seq_flag;
  guint8 progressive_source_flag;
  guint8 interlaced_source_flag;

  /* Picture currently being processed/decoded */
  GstH266Picture *current_picture;
  GstVideoCodecFrame *current_frame;

  GstH266Slice current_slice;

  gboolean new_bitstream_or_got_eos;
  gboolean no_output_before_recovery_flag;
  gint gdr_recovery_point_poc;
  gboolean no_output_of_prior_pics_flag;
  gint prev_tid0_pic;
  /* PicOrderCount of the previously outputted frame */
  gint last_output_poc;
  guint32 SpsMaxLatencyPictures;

  GstH266FrameFieldInfo ff_info;

  GArray *slices;

  gboolean aps_added[GST_H266_APS_TYPE_MAX][8];

  /* For delayed output */
  guint preferred_output_delay;
  gboolean is_live;
  GstQueueArray *output_queue;

  gboolean input_state_changed;

  GstFlowReturn last_flow;
};

typedef struct
{
  /* Holds ref */
  GstVideoCodecFrame *frame;
  GstH266Picture *picture;
  /* Without ref */
  GstH266Decoder *self;
} GstH266DecoderOutputFrame;

#define UPDATE_FLOW_RETURN(ret,new_ret) G_STMT_START { \
  if (*(ret) == GST_FLOW_OK) \
    *(ret) = new_ret; \
} G_STMT_END

#define parent_class gst_h266_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH266Decoder, gst_h266_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstH266Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_h266_decoder_debug, "h266decoder", 0,
        "H.266 Video Decoder"));

typedef struct
{
  const gchar *level_name;
  guint8 level_idc;
  guint32 MaxLumaPs;
} GstH266LevelLimits;

/* *INDENT-OFF* */
/* Table A.2 - General tier and level limits */
static const GstH266LevelLimits level_limits[] = {
  /* level    idc                   MaxLumaPs */
  {  "1.0",   GST_H266_LEVEL_L1_0,  36864    },
  {  "2.0",   GST_H266_LEVEL_L2_0,  122880   },
  {  "2.1",   GST_H266_LEVEL_L2_1,  245760   },
  {  "3.0",   GST_H266_LEVEL_L3_0,  552960   },
  {  "3.1",   GST_H266_LEVEL_L3_1,  983040   },
  {  "4.0",   GST_H266_LEVEL_L4_0,  2228224  },
  {  "4.1",   GST_H266_LEVEL_L4_1,  2228224  },
  {  "5.0",   GST_H266_LEVEL_L5_0,  8912896  },
  {  "5.1",   GST_H266_LEVEL_L5_1,  8912896  },
  {  "5.2",   GST_H266_LEVEL_L5_2,  8912896  },
  {  "6.0",   GST_H266_LEVEL_L6_0,  35651584 },
  {  "6.1",   GST_H266_LEVEL_L6_1,  35651584 },
  {  "6.2",   GST_H266_LEVEL_L6_2,  35651584 },
  {  "6.3",   GST_H266_LEVEL_L6_3,  80216064 },
};
/* *INDENT-ON* */

static gboolean
gst_h266_decoder_start (GstVideoDecoder * decoder)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);
  GstH266DecoderPrivate *priv = self->priv;

  priv->parser = gst_h266_parser_new ();
  priv->dpb = gst_h266_dpb_new ();
  priv->new_bitstream_or_got_eos = TRUE;
  priv->last_flow = GST_FLOW_OK;

  return TRUE;
}

static void
gst_h266_decoder_init_refs (GstH266Decoder * self)
{
  guint i, j;

  for (i = 0; i < 2; i++) {
    self->NumRefIdxActive[i] = 0;

    for (j = 0; j < GST_H266_MAX_REF_ENTRIES; j++) {
      self->RefPicList[i][j] = NULL;
      self->RefPicPocList[i][j] = G_MININT32;
      self->RefPicLtPocList[i][j] = G_MININT32;
      self->inter_layer_ref[i][j] = FALSE;
      self->RefPicScale[i][j][0] = 0;
      self->RefPicScale[i][j][1] = 0;
      self->RprConstraintsActiveFlag[i][j] = FALSE;
    }
  }
}

static gboolean
gst_h266_decoder_stop (GstVideoDecoder * decoder)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);
  GstH266DecoderPrivate *priv = self->priv;

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  if (priv->parser) {
    gst_h266_parser_free (priv->parser);
    priv->parser = NULL;
  }

  if (priv->dpb) {
    gst_h266_dpb_free (priv->dpb);
    priv->dpb = NULL;
  }

  return TRUE;
}

static void
gst_h266_decoder_format_from_caps (GstH266Decoder * self, GstCaps * caps,
    GstH266DecoderFormat * format, GstH266DecoderAlign * align)
{
  if (format)
    *format = GST_H266_DECODER_FORMAT_NONE;

  if (align)
    *align = GST_H266_DECODER_ALIGN_NONE;

  if (!gst_caps_is_fixed (caps)) {
    GST_WARNING_OBJECT (self, "Caps wasn't fixed");
    return;
  }

  GST_DEBUG_OBJECT (self, "parsing caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if (format) {
      if ((str = gst_structure_get_string (s, "stream-format"))) {
        if (strcmp (str, "vvc1") == 0)
          *format = GST_H266_DECODER_FORMAT_VVC1;
        else if (strcmp (str, "vvi1") == 0)
          *format = GST_H266_DECODER_FORMAT_VVI1;
        else if (strcmp (str, "byte-stream") == 0)
          *format = GST_H266_DECODER_FORMAT_BYTE;
      }
    }

    if (align) {
      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0)
          *align = GST_H266_DECODER_ALIGN_AU;
        else if (strcmp (str, "nal") == 0)
          *align = GST_H266_DECODER_ALIGN_NAL;
      }
    }
  }
}

static gboolean
gst_h266_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);
  GstH266DecoderPrivate *priv = self->priv;
  GstQuery *query;

  GST_DEBUG_OBJECT (decoder, "Set format");

  priv->input_state_changed = TRUE;

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);

  self->input_state = gst_video_codec_state_ref (state);

  priv->is_live = FALSE;
  query = gst_query_new_latency ();
  if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (self), query))
    gst_query_parse_latency (query, &priv->is_live, NULL, NULL);
  gst_query_unref (query);

  if (state->caps) {
    GstH266DecoderFormat format;
    GstH266DecoderAlign align;

    gst_h266_decoder_format_from_caps (self, state->caps, &format, &align);

    if (format == GST_H266_DECODER_FORMAT_NONE) {
      /* codec_data implies packetized */
      if (state->codec_data) {
        GST_WARNING_OBJECT (self,
            "video/x-h266 caps with codec_data but no stream-format=vvi1 or vvc1");
        format = GST_H266_DECODER_FORMAT_VVC1;
      } else {
        /* otherwise assume bytestream input */
        GST_WARNING_OBJECT (self,
            "video/x-h266 caps without codec_data or stream-format");
        format = GST_H266_DECODER_FORMAT_BYTE;
      }
    }

    if (format == GST_H266_DECODER_FORMAT_VVC1 ||
        format == GST_H266_DECODER_FORMAT_VVI1) {
      if (!state->codec_data) {
        /* Try it with size 4 anyway */
        priv->nal_length_size = 4;
        GST_WARNING_OBJECT (self,
            "packetized format without codec data, assuming nal length size is 4");
      }

      /* VVC1 implies alignment=au */
      if (align == GST_H266_DECODER_ALIGN_NONE)
        align = GST_H266_DECODER_ALIGN_AU;
    }

    if (format == GST_H266_DECODER_FORMAT_BYTE && state->codec_data)
      GST_WARNING_OBJECT (self, "bytestream with codec data");

    priv->in_format = format;
    priv->align = align;
  }

  if (state->codec_data) {
    /* TODO: */
    GST_WARNING_OBJECT (self, "vvc1 or vvi1 mode is not supported now.");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h266_decoder_negotiate (GstVideoDecoder * decoder)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);

  /* output state must be updated by subclass using new input state already */
  self->priv->input_state_changed = FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static void
gst_h266_decoder_set_latency (GstH266Decoder * self, const GstH266SPS * sps,
    gint max_dpb_size)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstCaps *caps;
  GstClockTime min, max;
  GstStructure *structure;
  gint fps_d = 1, fps_n = 0;
  guint frames_delay;

  caps = gst_pad_get_current_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  if (!caps && self->input_state)
    caps = gst_caps_ref (self->input_state->caps);

  if (caps) {
    structure = gst_caps_get_structure (caps, 0);
    if (gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d)) {
      if (fps_n == 0) {
        /* variable framerate: see if we have a max-framerate */
        gst_structure_get_fraction (structure, "max-framerate", &fps_n, &fps_d);
      }
    }
    gst_caps_unref (caps);
  }

  /* if no fps or variable, then 25/1 */
  if (fps_n == 0) {
    fps_n = 25;
    fps_d = 1;
  }

  /* Minimum possible latency could be calculated based on C.5.2.3
   * 1) # of pictures (marked as "needed for output") in DPB > sps_max_num_reorder_pics
   *   - We will assume all pictures in DPB are marked as "needed for output"
   * 2) sps_max_latency_increase_plus1 != 0 and
   *    PicLatencyCount >= SpsMaxLatencyPictures
   *   - SpsMaxLatencyPictures is equal to
   *     "sps_max_num_reorder_pics + sps_max_latency_increase_plus1 - 1"
   *     and PicLatencyCount of each picture in DPB is increased by 1 per
   *     decoding loop. Note that PicLatencyCount of the currently decoded
   *     picture is zero. So, in case that all pictures in DPB are marked as
   *     "needed for output", Only condition 1) will have an effect
   *     regardless of sps_max_latency_increase_plus1.
   *
   *     For example, assume sps_max_num_reorder_pics is 2 and
   *     sps_max_latency_increase_plus1 is 1, then SpsMaxLatencyPictures is 2.
   *     For a picture in DPB to have PicLatencyCount >= SpsMaxLatencyPictures,
   *     there must be at least 3 pictures including current picture in DPB
   *     (current picture's PicLatencyCount is zero).
   *     This is already covered by the condition 1). So, this condition 2)
   *     will have effect only when there are pictures marked as
   *     "not needed for output" in DPB.
   *
   *  Thus, we can take sps_max_num_reorder_pics as a min latency value
   */
  frames_delay = sps->dpb.max_num_reorder_pics[sps->max_sublayers_minus1];

  /* Consider output delay wanted by subclass */
  frames_delay += priv->preferred_output_delay;

  min = gst_util_uint64_scale_int (frames_delay * GST_SECOND, fps_d, fps_n);
  max = gst_util_uint64_scale_int ((max_dpb_size + priv->preferred_output_delay)
      * GST_SECOND, fps_d, fps_n);

  GST_DEBUG_OBJECT (self,
      "latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT
      " min-frames-delay %d", GST_TIME_ARGS (min), GST_TIME_ARGS (max),
      frames_delay);

  gst_video_decoder_set_latency (GST_VIDEO_DECODER (self), min, max);
}

static void
gst_h266_decoder_reset_frame_state (GstH266Decoder * self)
{
  GstH266DecoderPrivate *priv = self->priv;
  gint i;

  /* Clear picture struct information */
  memset (&priv->ff_info, 0, sizeof (GstH266FrameFieldInfo));
  priv->ff_info.source_scan_type = 2;

  priv->current_frame = NULL;

  g_array_set_size (priv->slices, 0);

  for (i = 0; i < GST_H266_APS_TYPE_MAX; i++)
    g_array_set_size (self->aps_list[i], 0);
  memset (priv->aps_added, 0, sizeof (priv->aps_added));

  gst_h266_decoder_init_refs (self);
}

static GstH266ParserResult
gst_h266_decoder_parse_sei (GstH266Decoder * self, GstH266NalUnit * nalu)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstH266ParserResult pres;
  GArray *messages = NULL;

  pres = gst_h266_parser_parse_sei (priv->parser, nalu, &messages);
  if (pres != GST_H266_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SEI, result %d", pres);

    /* XXX: Ignore error from SEI parsing, it might be malformed bitstream,
     * or our fault. But shouldn't be critical  */
    g_clear_pointer (&messages, g_array_unref);
    return GST_H266_PARSER_OK;
  }

  /* TODO: */

  return GST_H266_PARSER_OK;
}

static GstH266ParserResult
gst_h266_decoder_parse_slice (GstH266Decoder * self, GstH266NalUnit * nalu)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstH266ParserResult pres;
  GstH266Slice slice;

  memset (&slice, 0, sizeof (GstH266Slice));

  pres = gst_h266_parser_parse_slice_hdr (priv->parser, nalu, &slice.header);
  if (pres != GST_H266_PARSER_OK)
    return pres;

  slice.nalu = *nalu;

  if (slice.header.picture_header_in_slice_header_flag) {
    slice.first_slice = TRUE;

    if (priv->slices->len > 0) {
      GST_WARNING_OBJECT (self,
          "A problematic stream has internal PH for multi slices.");
      slice.first_slice = FALSE;
    }
  } else if (priv->slices->len == 0) {
    slice.first_slice = TRUE;
  }

  /* C.3.2 */
  slice.no_output_of_prior_pics_flag =
      slice.header.no_output_of_prior_pics_flag;

  if (slice.first_slice) {
    /* 8.1.1 */
    if (GST_H266_IS_NAL_TYPE_IDR (slice.nalu.type)) {
      priv->no_output_before_recovery_flag = FALSE;
    } else if (GST_H266_IS_NAL_TYPE_CRA (slice.nalu.type) ||
        GST_H266_IS_NAL_TYPE_GDR (slice.nalu.type)) {
      priv->no_output_before_recovery_flag = priv->new_bitstream_or_got_eos;
    }

    priv->no_output_of_prior_pics_flag = slice.no_output_of_prior_pics_flag;
  } else {
    if (priv->no_output_of_prior_pics_flag !=
        slice.no_output_of_prior_pics_flag)
      GST_WARNING_OBJECT (self, "A problematic stream has different "
          "no_output_of_prior_pics_flag within one AU.");
    priv->no_output_of_prior_pics_flag |= slice.no_output_of_prior_pics_flag;
  }

  if (GST_H266_IS_NAL_TYPE_IRAP (slice.nalu.type) &&
      !priv->new_bitstream_or_got_eos)
    slice.clear_dpb = TRUE;

  slice.no_output_before_recovery_flag = priv->no_output_before_recovery_flag;

  priv->new_bitstream_or_got_eos = FALSE;
  g_array_append_val (priv->slices, slice);

  return GST_H266_PARSER_OK;
}

static GstH266ParserResult
gst_h266_decoder_parse_nalu (GstH266Decoder * self, GstH266NalUnit * nalu)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstH266VPS vps;
  GstH266SPS sps;
  GstH266PPS pps;
  GstH266APS aps;
  GstH266PicHdr ph;
  GstH266ParserResult ret = GST_H266_PARSER_OK;

  GST_LOG_OBJECT (self, "Parsed nal type: %d, offset %d, size %d",
      nalu->type, nalu->offset, nalu->size);

  switch (nalu->type) {
    case GST_H266_NAL_VPS:
      ret = gst_h266_parser_parse_vps (priv->parser, nalu, &vps);
      break;
    case GST_H266_NAL_SPS:
      ret = gst_h266_parser_parse_sps (priv->parser, nalu, &sps);
      break;
    case GST_H266_NAL_PPS:
      ret = gst_h266_parser_parse_pps (priv->parser, nalu, &pps);
      break;
    case GST_H266_NAL_PH:
      ret = gst_h266_parser_parse_picture_hdr (priv->parser, nalu, &ph);
      break;
    case GST_H266_NAL_PREFIX_SEI:
    case GST_H266_NAL_SUFFIX_SEI:
      ret = gst_h266_decoder_parse_sei (self, nalu);
      break;
    case GST_H266_NAL_PREFIX_APS:
    case GST_H266_NAL_SUFFIX_APS:
      ret = gst_h266_parser_parse_aps (priv->parser, nalu, &aps);
      break;
    case GST_H266_NAL_SLICE_TRAIL:
    case GST_H266_NAL_SLICE_STSA:
    case GST_H266_NAL_SLICE_RADL:
    case GST_H266_NAL_SLICE_RASL:
    case GST_H266_NAL_SLICE_IDR_W_RADL:
    case GST_H266_NAL_SLICE_IDR_N_LP:
    case GST_H266_NAL_SLICE_CRA:
    case GST_H266_NAL_SLICE_GDR:
      ret = gst_h266_decoder_parse_slice (self, nalu);
      break;
    case GST_H266_NAL_EOB:
    case GST_H266_NAL_EOS:
      /* TODO: drain the DPB */
      priv->new_bitstream_or_got_eos = TRUE;
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_h266_decoder_preprocess_slice (GstH266Decoder * self, GstH266Slice * slice)
{
  GstH266DecoderPrivate *priv = self->priv;

  if (priv->current_picture && slice->first_slice) {
    GST_WARNING_OBJECT (self, "Current picture is not finished but slice "
        "header has first_slice_segment_in_pic_flag");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gint
gst_h266_decoder_get_max_dpb_size_from_sps (GstH266Decoder * self,
    GstH266SPS * sps)
{
  guint i;
  guint PicSizeMaxInSamplesY;
  /* Default is the worst case level 6.2 */
  guint32 MaxLumaPs = G_MAXUINT32;
  const gint maxDpbPicBuf = 8;
  gint MaxDpbSize;

  /* Unknown level */
  if (sps->profile_tier_level.level_idc == 0)
    return GST_H266_MAX_DPB_SIZE;

  PicSizeMaxInSamplesY =
      sps->pic_width_max_in_luma_samples * sps->pic_height_max_in_luma_samples;

  for (i = 0; i < G_N_ELEMENTS (level_limits); i++) {
    if (sps->profile_tier_level.level_idc <= level_limits[i].level_idc) {
      if (PicSizeMaxInSamplesY <= level_limits[i].MaxLumaPs) {
        MaxLumaPs = level_limits[i].MaxLumaPs;
      } else {
        GST_DEBUG_OBJECT (self,
            "%u (%dx%d) exceeds allowed max luma sample for level \"%s\" %u",
            PicSizeMaxInSamplesY, sps->pic_width_max_in_luma_samples,
            sps->pic_height_max_in_luma_samples, level_limits[i].level_name,
            level_limits[i].MaxLumaPs);
      }
      break;
    }
  }

  /* Unknown level */
  if (MaxLumaPs == G_MAXUINT32)
    return GST_H266_MAX_DPB_SIZE;

  /* A.4.2 */
  if (2 * PicSizeMaxInSamplesY <= MaxLumaPs)
    MaxDpbSize = 2 * maxDpbPicBuf;
  else if (3 * PicSizeMaxInSamplesY <= 2 * MaxLumaPs)
    MaxDpbSize = 3 * maxDpbPicBuf / 2;
  else
    MaxDpbSize = maxDpbPicBuf;

  return MIN (MaxDpbSize, GST_H266_MAX_DPB_SIZE);
}

static gboolean
gst_h266_decoder_is_crop_rect_changed (GstH266Decoder * self, GstH266SPS * sps)
{
  GstH266DecoderPrivate *priv = self->priv;

  if (priv->conformance_window_flag != sps->conformance_window_flag)
    return TRUE;
  if (priv->crop_rect_width != sps->crop_rect_width)
    return TRUE;
  if (priv->crop_rect_height != sps->crop_rect_height)
    return TRUE;
  if (priv->crop_rect_x != sps->crop_rect_x)
    return TRUE;
  if (priv->crop_rect_y != sps->crop_rect_y)
    return TRUE;

  return FALSE;
}

static void
gst_h266_decoder_drain_output_queue (GstH266Decoder * self, guint num,
    GstFlowReturn * ret)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstH266DecoderClass *klass = GST_H266_DECODER_GET_CLASS (self);

  g_assert (klass->output_picture);
  g_assert (ret != NULL);

  while (gst_queue_array_get_length (priv->output_queue) > num) {
    GstH266DecoderOutputFrame *output_frame = (GstH266DecoderOutputFrame *)
        gst_queue_array_pop_head_struct (priv->output_queue);
    GstFlowReturn flow_ret = klass->output_picture (self, output_frame->frame,
        output_frame->picture);

    UPDATE_FLOW_RETURN (ret, flow_ret);
  }
}

static void
gst_h266_decoder_clear_output_frame (GstH266DecoderOutputFrame * output_frame)
{
  if (!output_frame)
    return;

  if (output_frame->frame) {
    gst_video_decoder_release_frame (GST_VIDEO_DECODER (output_frame->self),
        output_frame->frame);
    output_frame->frame = NULL;
  }

  gst_clear_h266_picture (&output_frame->picture);
}

static void
gst_h266_decoder_clear_dpb (GstH266Decoder * self, gboolean flush)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (self);
  GstH266DecoderPrivate *priv = self->priv;
  GstH266Picture *picture;

  /* If we are not flushing now, videodecoder baseclass will hold
   * GstVideoCodecFrame. Release frames manually */
  if (!flush) {
    while ((picture = gst_h266_dpb_bump (priv->dpb, TRUE)) != NULL) {
      GstVideoCodecFrame *frame = gst_video_decoder_get_frame (decoder,
          GST_CODEC_PICTURE_FRAME_NUMBER (picture));

      if (frame)
        gst_video_decoder_release_frame (decoder, frame);
      gst_h266_picture_unref (picture);
    }
  }

  gst_queue_array_clear (priv->output_queue);
  gst_h266_dpb_clear (priv->dpb);
  priv->last_output_poc = G_MININT32;
}

static void
gst_h266_decoder_do_output_picture (GstH266Decoder * self,
    GstH266Picture * picture, GstFlowReturn * ret)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstVideoCodecFrame *frame = NULL;
  GstH266DecoderOutputFrame output_frame;

  g_assert (ret != NULL);

  GST_LOG_OBJECT (self, "Output picture %p (poc %d)", picture,
      picture->pic_order_cnt);

  if (picture->pic_order_cnt < priv->last_output_poc) {
    GST_WARNING_OBJECT (self,
        "Outputting out of order %d -> %d, likely a broken stream",
        priv->last_output_poc, picture->pic_order_cnt);
  }

  priv->last_output_poc = picture->pic_order_cnt;

  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (self),
      GST_CODEC_PICTURE_FRAME_NUMBER (picture));

  if (!frame) {
    GST_ERROR_OBJECT (self,
        "No available codec frame with frame number %d",
        GST_CODEC_PICTURE_FRAME_NUMBER (picture));
    UPDATE_FLOW_RETURN (ret, GST_FLOW_ERROR);

    gst_h266_picture_unref (picture);
    return;
  }

  output_frame.frame = frame;
  output_frame.picture = picture;
  output_frame.self = self;
  gst_queue_array_push_tail_struct (priv->output_queue, &output_frame);

  gst_h266_decoder_drain_output_queue (self, priv->preferred_output_delay,
      &priv->last_flow);
}

static gboolean
gst_h266_decoder_flush (GstVideoDecoder * decoder)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);

  gst_h266_decoder_clear_dpb (self, TRUE);

  return TRUE;
}

static GstFlowReturn
gst_h266_decoder_drain_internal (GstH266Decoder * self)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstH266Picture *picture;
  GstFlowReturn ret = GST_FLOW_OK;

  while ((picture = gst_h266_dpb_bump (priv->dpb, TRUE)) != NULL)
    gst_h266_decoder_do_output_picture (self, picture, &ret);

  gst_h266_decoder_drain_output_queue (self, 0, &ret);

  gst_h266_dpb_clear (priv->dpb);
  priv->last_output_poc = G_MININT32;

  return ret;
}

static GstFlowReturn
gst_h266_decoder_drain (GstVideoDecoder * decoder)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);

  /* dpb will be cleared by this method */
  return gst_h266_decoder_drain_internal (self);
}

static GstFlowReturn
gst_h266_decoder_finish (GstVideoDecoder * decoder)
{
  return gst_h266_decoder_drain (decoder);
}

static GstFlowReturn
gst_h266_decoder_process_sps (GstH266Decoder * self, GstH266SPS * sps)
{
  GstH266DecoderPrivate *priv = self->priv;
  gint max_dpb_size, prev_max_dpb_size;
  guint8 field_seq_flag;
  guint8 progressive_source_flag = 0, interlaced_source_flag = 0;
  GstFlowReturn ret = GST_FLOW_OK;

  max_dpb_size = gst_h266_decoder_get_max_dpb_size_from_sps (self, sps);
  prev_max_dpb_size = gst_h266_dpb_get_max_num_pics (priv->dpb);

  field_seq_flag = sps->field_seq_flag;
  if (sps->vui_parameters_present_flag) {
    progressive_source_flag = sps->vui_params.progressive_source_flag;
    interlaced_source_flag = sps->vui_params.interlaced_source_flag;
  }

  if (priv->max_width != sps->max_width ||
      priv->max_height != sps->max_height ||
      prev_max_dpb_size != max_dpb_size ||
      priv->field_seq_flag != field_seq_flag ||
      priv->progressive_source_flag != progressive_source_flag ||
      priv->interlaced_source_flag != interlaced_source_flag ||
      gst_h266_decoder_is_crop_rect_changed (self, sps)) {
    GstH266DecoderClass *klass = GST_H266_DECODER_GET_CLASS (self);

    GST_DEBUG_OBJECT (self,
        "SPS updated, resolution: %dx%d -> %dx%d, dpb size: %d -> %d, "
        "field_seq_flag: %d -> %d, progressive_source_flag: %d -> %d, "
        "interlaced_source_flag: %d -> %d",
        priv->max_width, priv->max_height, sps->max_width, sps->max_height,
        prev_max_dpb_size, max_dpb_size, priv->field_seq_flag, field_seq_flag,
        priv->progressive_source_flag, progressive_source_flag,
        priv->interlaced_source_flag, interlaced_source_flag);

    if (priv->no_output_of_prior_pics_flag) {
      gst_h266_decoder_drain_output_queue (self, 0, &ret);
      gst_h266_decoder_clear_dpb (self, FALSE);
    } else {
      ret = gst_h266_decoder_drain_internal (self);
    }

    if (ret != GST_FLOW_OK)
      return ret;

    if (klass->get_preferred_output_delay) {
      priv->preferred_output_delay =
          klass->get_preferred_output_delay (self, priv->is_live);
    } else {
      priv->preferred_output_delay = 0;
    }

    g_assert (klass->new_sequence);
    ret = klass->new_sequence (self,
        sps, max_dpb_size + priv->preferred_output_delay);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "subclass does not want accept new sequence");
      return ret;
    }

    priv->max_width = sps->max_width;
    priv->max_height = sps->max_height;
    priv->conformance_window_flag = sps->conformance_window_flag;
    priv->crop_rect_width = sps->crop_rect_width;
    priv->crop_rect_height = sps->crop_rect_height;
    priv->crop_rect_x = sps->crop_rect_x;
    priv->crop_rect_y = sps->crop_rect_y;
    priv->field_seq_flag = field_seq_flag;
    priv->progressive_source_flag = progressive_source_flag;
    priv->interlaced_source_flag = interlaced_source_flag;

    gst_h266_dpb_set_max_num_pics (priv->dpb, max_dpb_size);
    gst_h266_decoder_set_latency (self, sps, max_dpb_size);

    GST_DEBUG_OBJECT (self, "Set DPB max size %d", max_dpb_size);
  }

  if (sps->dpb.max_latency_increase_plus1[sps->max_sublayers_minus1]) {
    priv->SpsMaxLatencyPictures =
        sps->dpb.max_num_reorder_pics[sps->max_sublayers_minus1] +
        sps->dpb.max_latency_increase_plus1[sps->max_sublayers_minus1] - 1;
  } else {
    priv->SpsMaxLatencyPictures = 0;
  }

  return GST_FLOW_OK;
}

static void
gst_h266_decoder_calculate_poc (GstH266Decoder * self,
    const GstH266Slice * slice, GstH266Picture * picture)
{
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266SPS *sps = priv->parser->active_sps;
  gint32 max_poc_lsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  gint32 prev_poc_lsb = priv->prev_tid0_pic % max_poc_lsb;
  gint32 prev_poc_msb = priv->prev_tid0_pic - prev_poc_lsb;
  gint32 poc_lsb = slice->header.picture_header.pic_order_cnt_lsb;
  gint32 poc_msb;

  /* 8.3.1 Decoding process for picture order count */
  if (slice->header.picture_header.poc_msb_cycle_present_flag) {
    poc_msb = slice->header.picture_header.poc_msb_cycle_val * max_poc_lsb;
  } else if (GST_H266_IS_NAL_TYPE_CVSS (slice->nalu.type)
      && slice->no_output_before_recovery_flag) {
    poc_msb = 0;
  } else {
    if (poc_lsb < prev_poc_lsb && prev_poc_lsb - poc_lsb >= max_poc_lsb / 2)
      poc_msb = prev_poc_msb + max_poc_lsb;
    else if (poc_lsb > prev_poc_lsb && poc_lsb - prev_poc_lsb > max_poc_lsb / 2)
      poc_msb = prev_poc_msb - max_poc_lsb;
    else
      poc_msb = prev_poc_msb;
  }

  picture->pic_order_cnt = poc_msb + poc_lsb;
  picture->pic_order_cnt_msb = poc_msb;
  picture->pic_order_cnt_lsb = poc_lsb;
}

static void
gst_h266_decoder_set_buffer_flags (GstH266Decoder * self,
    GstH266Picture * picture)
{
  GstH266DecoderPrivate *priv = self->priv;

  if (!priv->ff_info.valid) {
    if (priv->field_seq_flag) {
      GST_FIXME_OBJECT (self, "When sps_field_seq_flag is equal to 1, a "
          "frame-field information SEI message shall be present for every "
          "coded picture in the CLVS.");
    }
    return;
  }

  picture->ff_info = priv->ff_info;

  if (priv->ff_info.field_pic_flag) {
    if (priv->ff_info.bottom_field_flag) {
      picture->buffer_flags |= GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD;
    } else {
      picture->buffer_flags |= GST_VIDEO_BUFFER_FLAG_TOP_FIELD;
    }
  } else {
    if (priv->ff_info.display_fields_from_frame_flag) {
      picture->buffer_flags |= GST_VIDEO_BUFFER_FLAG_INTERLACED;
      if (priv->ff_info.top_field_first_flag)
        picture->buffer_flags |= GST_VIDEO_BUFFER_FLAG_TFF;
    } else {
      if (priv->field_seq_flag) {
        GST_FIXME_OBJECT (self, "frame-field information SEI message indicate "
            "a complete frame but sps_field_seq_flag indicate the field only "
            "stream.");
      }
    }
  }
}

static gboolean
gst_h266_decoder_init_current_picture (GstH266Decoder * self)
{
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266Slice *slice = &priv->current_slice;
  GstH266Picture *picture = priv->current_picture;

  gst_h266_decoder_calculate_poc (self, slice, picture);

  picture->NoOutputBeforeRecoveryFlag = slice->no_output_before_recovery_flag;
  picture->NoOutputOfPriorPicsFlag = slice->no_output_of_prior_pics_flag;
  picture->type = slice->header.slice_type;
  picture->non_ref = slice->header.picture_header.non_ref_pic_flag;

  gst_h266_decoder_set_buffer_flags (self, picture);

  return TRUE;
}

static GstFlowReturn
gst_h266_decoder_prepare_rpl (GstH266Decoder * self, const GstH266Slice * slice,
    GstH266Picture * picture, gboolean new_picture)
{
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266RefPicLists *rpls = &slice->header.ref_pic_lists;
  const GstH266RefPicListStruct *ref_list;
  gint32 max_poc_lsb =
      1 << (priv->parser->active_sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  guint32 prev_delta_poc_msb, delta_poc_msb_cycle_lt;
  gint poc_base, poc;
  guint collocated_list =
      slice->header.picture_header.collocated_from_l0_flag ? 0 : 1;
  guint i, j;
  GstH266Picture *ref_pic;

  if (new_picture)
    gst_h266_dpb_mark_all_non_ref (priv->dpb);

  gst_h266_decoder_init_refs (self);

  for (i = 0; i < 2; i++) {
    ref_list = &rpls->rpl_ref_list[i];
    poc_base = picture->pic_order_cnt;
    prev_delta_poc_msb = 0;

    for (j = 0; j < ref_list->num_ref_entries; j++) {
      if (ref_list->inter_layer_ref_pic_flag[j]) {
        GST_WARNING_OBJECT (self,
            "Inter layer reference is not supported now.");
        return GST_FLOW_NOT_SUPPORTED;
      }

      ref_pic = NULL;

      if (ref_list->st_ref_pic_flag[j]) {
        poc = poc_base + ref_list->delta_poc_val_st[j];
        self->RefPicPocList[i][j] = poc;

        ref_pic = gst_h266_dpb_get_picture_by_poc (priv->dpb, poc);

        if (!ref_pic) {
          GST_WARNING_OBJECT (self,
              "Missing a short term reference of poc: %d", poc);
        } else {
          if (ref_pic->non_ref) {
            GST_WARNING_OBJECT (self, "non ref picture should not be "
                "marked as reference");
          }

          ref_pic->ref = TRUE;
          self->RefPicList[i][j] = ref_pic;
        }

        poc_base = poc;
      } else {
        if (!ref_list->ltrp_in_header_flag) {
          poc = ref_list->rpls_poc_lsb_lt[j];
        } else {
          poc = rpls->poc_lsb_lt[i][j];
        }

        if (rpls->delta_poc_msb_cycle_present_flag[i][j]) {
          delta_poc_msb_cycle_lt = rpls->delta_poc_msb_cycle_lt[i][j];
          delta_poc_msb_cycle_lt += prev_delta_poc_msb;
          poc += picture->pic_order_cnt - delta_poc_msb_cycle_lt * max_poc_lsb -
              (picture->pic_order_cnt & (max_poc_lsb - 1));
          prev_delta_poc_msb = delta_poc_msb_cycle_lt;
        }

        self->RefPicLtPocList[i][j] = poc;

        if (rpls->delta_poc_msb_cycle_present_flag[i][j]) {
          ref_pic = gst_h266_dpb_get_picture_by_poc (priv->dpb, poc);
        } else {
          ref_pic = gst_h266_dpb_get_picture_by_poc_lsb (priv->dpb, poc);
        }

        if (!ref_pic) {
          GST_WARNING_OBJECT (self,
              "Missing a long term reference of poc: %d", poc);
        } else {
          if (ref_pic->non_ref) {
            GST_WARNING_OBJECT (self, "non ref picture should not be "
                "marked as reference");
          }

          ref_pic->ref = TRUE;
          ref_pic->long_term = TRUE;
          self->RefPicList[i][j] = ref_pic;
        }
      }

      if (ref_pic)
        gst_h266_picture_unref (ref_pic);
    }

    /* the first NumRefIdxActive[i] entries in RefPicList[i] are
       referred to as the active entries in RefPicList[i], and the
       other entries in RefPicList[i] are referred to as the inactive
       entries in RefPicList[i]. */
    self->NumRefIdxActive[i] = slice->header.num_ref_idx_active[i];

    if (collocated_list != i)
      continue;

    if (slice->header.picture_header.temporal_mvp_enabled_flag) {
      if (slice->header.collocated_ref_idx > self->NumRefIdxActive[i] - 1 ||
          self->RefPicList[i][slice->header.collocated_ref_idx] == NULL) {
        GST_WARNING_OBJECT (self, "Missing the collocated reference of "
            "index: %d in reference list: %d.",
            slice->header.collocated_ref_idx, i);
      }
    }
  }

  return GST_FLOW_OK;
}

/* C.5.2.2 */
static GstFlowReturn
gst_h266_decoder_dpb_init (GstH266Decoder * self, const GstH266Slice * slice,
    GstH266Picture * picture)
{
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266SPS *sps = priv->parser->active_sps;
  GstH266Picture *to_output;
  GstFlowReturn ret = GST_FLOW_OK;

  /* C 3.2 */
  if (slice->clear_dpb) {
    if (picture->NoOutputOfPriorPicsFlag) {
      GST_DEBUG_OBJECT (self, "Clear dpb");
      gst_h266_decoder_drain_output_queue (self, 0, &priv->last_flow);
      gst_h266_decoder_clear_dpb (self, FALSE);
    } else {
      gst_h266_dpb_delete_unused (priv->dpb);

      while ((to_output = gst_h266_dpb_bump (priv->dpb, FALSE)) != NULL)
        gst_h266_decoder_do_output_picture (self, to_output, &ret);

      if (gst_h266_dpb_get_size (priv->dpb) > 0) {
        /* For CRA with NoOutputOfPriorPicsFlag=0, the previous pictures
           can still be references and following pictures may be RASL. */
        if (!GST_H266_IS_NAL_TYPE_CRA (slice->nalu.type)) {
          GST_WARNING_OBJECT (self, "IDR frame failed to clear the dpb, "
              "there are still %d pictures in the dpb, last output poc is %d",
              gst_h266_dpb_get_size (priv->dpb), priv->last_output_poc);
        }
      } else {
        priv->last_output_poc = G_MININT32;
      }
    }
  } else {
    gst_h266_dpb_delete_unused (priv->dpb);

    while (gst_h266_dpb_needs_bump (priv->dpb,
            sps->dpb.max_num_reorder_pics[sps->max_sublayers_minus1],
            priv->SpsMaxLatencyPictures,
            sps->dpb.max_dec_pic_buffering_minus1[sps->max_sublayers_minus1] +
            1)) {
      to_output = gst_h266_dpb_bump (priv->dpb, FALSE);

      /* Something wrong... */
      if (!to_output) {
        GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
        break;
      }

      gst_h266_decoder_do_output_picture (self, to_output, &ret);
    }
  }

  return ret;
}

static gboolean
gst_h266_decoder_add_aps (GstH266Decoder * self,
    GstH266APSType aps_type, guint8 aps_id)
{
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266Parser *parser = priv->parser;
  const GstH266APS *aps;

  g_assert (aps_id <= 7);

  aps = &parser->aps[aps_type][aps_id];
  if (!aps->valid) {
    GST_WARNING_OBJECT (self, "APS type %d, id %d is not valid.",
        aps_type, aps_id);
    return FALSE;
  }

  if (!priv->aps_added[aps_type][aps_id]) {
    priv->aps_added[aps_type][aps_id] = TRUE;
    g_array_append_val (self->aps_list[aps_type], aps);
  }

  return TRUE;
}

static gboolean
gst_h266_decoder_collect_aps_list (GstH266Decoder * self,
    const GstH266Slice * slice)
{
  guint8 aps_id;
  guint i;

  if (slice->header.alf_enabled_flag) {
    for (i = 0; i < slice->header.num_alf_aps_ids_luma; i++) {
      aps_id = slice->header.alf_aps_id_luma[i];
      if (!gst_h266_decoder_add_aps (self, GST_H266_ALF_APS, aps_id))
        return FALSE;
    }

    if (slice->header.alf_cb_enabled_flag || slice->header.alf_cr_enabled_flag) {
      aps_id = slice->header.alf_aps_id_chroma;
      if (!gst_h266_decoder_add_aps (self, GST_H266_ALF_APS, aps_id))
        return FALSE;
    }

    if (slice->header.alf_cc_cb_enabled_flag) {
      aps_id = slice->header.alf_cc_cb_aps_id;
      if (!gst_h266_decoder_add_aps (self, GST_H266_ALF_APS, aps_id))
        return FALSE;
    }

    if (slice->header.alf_cc_cr_enabled_flag) {
      aps_id = slice->header.alf_cc_cr_aps_id;
      if (!gst_h266_decoder_add_aps (self, GST_H266_ALF_APS, aps_id))
        return FALSE;
    }
  }

  if (slice->header.lmcs_used_flag) {
    aps_id = slice->header.picture_header.lmcs_aps_id;
    if (!gst_h266_decoder_add_aps (self, GST_H266_LMCS_APS, aps_id))
      return FALSE;
  }

  if (slice->header.explicit_scaling_list_used_flag) {
    aps_id = slice->header.picture_header.scaling_list_aps_id;
    if (!gst_h266_decoder_add_aps (self, GST_H266_SCALING_APS, aps_id))
      return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_h266_decoder_start_current_picture (GstH266Decoder * self)
{
  GstH266DecoderClass *klass;
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266Slice *slice = &priv->current_slice;
  GstH266Picture *picture = priv->current_picture;
  GstFlowReturn ret = GST_FLOW_OK;

  g_assert (priv->current_picture != NULL);
  g_assert (priv->parser->active_vps != NULL);
  g_assert (priv->parser->active_sps != NULL);
  g_assert (priv->parser->active_pps != NULL);

  if (!gst_h266_decoder_init_current_picture (self))
    return GST_FLOW_ERROR;

  picture->pps_width = priv->parser->active_pps->width;
  picture->pps_height = priv->parser->active_pps->height;
  picture->pps_conformance_window_flag =
      priv->parser->active_pps->conformance_window_flag;
  picture->pps_crop_rect_width = priv->parser->active_pps->crop_rect_width;
  picture->pps_crop_rect_height = priv->parser->active_pps->crop_rect_height;
  picture->pps_crop_rect_x = priv->parser->active_pps->crop_rect_x;
  picture->pps_crop_rect_y = priv->parser->active_pps->crop_rect_y;

  if (priv->no_output_before_recovery_flag) {
    if (GST_H266_IS_NAL_TYPE_IRAP (slice->nalu.type)) {
      priv->gdr_recovery_point_poc = G_MININT;
    } else if (GST_H266_IS_NAL_TYPE_GDR (slice->nalu.type)) {
      priv->gdr_recovery_point_poc = picture->pic_order_cnt +
          slice->header.picture_header.recovery_poc_cnt;
    }

    if (priv->gdr_recovery_point_poc != G_MININT &&
        priv->gdr_recovery_point_poc <= picture->pic_order_cnt)
      priv->gdr_recovery_point_poc = G_MININT;

    /* Drop all RASL pictures having NoRaslOutputFlag is TRUE. */
    if (GST_H266_IS_NAL_TYPE_RASL (slice->nalu.type)) {
      GST_DEBUG_OBJECT (self, "Drop current picture");
      gst_clear_h266_picture (&priv->current_picture);
      return GST_FLOW_OK;
    }
  }

  if ((slice->nalu.temporal_id_plus1 - 1 == 0) &&
      !slice->header.picture_header.non_ref_pic_flag &&
      !(GST_H266_IS_NAL_TYPE_RASL (slice->nalu.type) ||
          GST_H266_IS_NAL_TYPE_RADL (slice->nalu.type)))
    priv->prev_tid0_pic = picture->pic_order_cnt;

  if (priv->gdr_recovery_point_poc != G_MININT &&
      picture->pic_order_cnt < priv->gdr_recovery_point_poc) {
    g_assert (priv->no_output_before_recovery_flag);
    picture->output_flag = FALSE;
  } else if (slice->header.picture_header.pic_output_flag) {
    picture->output_flag = TRUE;
  } else {
    picture->output_flag = FALSE;
  }

  ret = gst_h266_decoder_prepare_rpl (self, slice, picture, TRUE);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Failed to prepare ref pic list");
    gst_clear_h266_picture (&priv->current_picture);
    return ret;
  }

  ret = gst_h266_decoder_dpb_init (self, slice, picture);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Failed to init dpb");
    gst_clear_h266_picture (&priv->current_picture);
    return ret;
  }

  klass = GST_H266_DECODER_GET_CLASS (self);

  if (klass->new_picture)
    ret = klass->new_picture (self, priv->current_frame, picture);

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "subclass does not want accept new picture");
    gst_clear_h266_picture (&priv->current_picture);
    return ret;
  }

  if (klass->start_picture) {
    ret = klass->start_picture (self, picture, &priv->current_slice, priv->dpb);

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "subclass does not want to start picture");
      gst_clear_h266_picture (&priv->current_picture);
      return ret;
    }
  }

  /* If subclass didn't update output state at this point,
   * marking this picture as a discont and stores current input state */
  if (priv->input_state_changed) {
    gst_h266_picture_set_discont_state (priv->current_picture,
        self->input_state);
    priv->input_state_changed = FALSE;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h266_decoder_decode_slice (GstH266Decoder * self)
{
  GstH266DecoderClass *klass = GST_H266_DECODER_GET_CLASS (self);
  GstH266DecoderPrivate *priv = self->priv;
  GstH266Slice *slice = &priv->current_slice;
  GstH266Picture *picture = priv->current_picture;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    return GST_FLOW_ERROR;
  }

  g_assert (klass->decode_slice);

  ret = klass->decode_slice (self, picture, slice);

  return ret;
}

static GstFlowReturn
gst_h266_decoder_process_slice (GstH266Decoder * self, GstH266Slice * slice)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;

  priv->current_slice = *slice;

  ret = gst_h266_decoder_preprocess_slice (self, &priv->current_slice);
  if (ret != GST_FLOW_OK)
    return ret;

  /* The used SPS may not be the latest parsed one, make
   * sure we have updated it before decode the current frame */
  ret = gst_h266_decoder_process_sps (self,
      priv->current_slice.header.picture_header.pps->sps);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Failed to process sps");
    return ret;
  }

  if (!priv->current_picture) {
    GstH266Picture *picture;
    GstFlowReturn ret = GST_FLOW_OK;

    g_assert (priv->current_frame);

    picture = gst_h266_picture_new ();
    /* This allows accessing the frame from the picture. */
    GST_CODEC_PICTURE_FRAME_NUMBER (picture) =
        priv->current_frame->system_frame_number;

    priv->current_picture = picture;

    ret = gst_h266_decoder_start_current_picture (self);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "start picture failed");
      return ret;
    }

    /* this picture was dropped */
    if (!priv->current_picture)
      return GST_FLOW_OK;
  } else {
    ret = gst_h266_decoder_prepare_rpl (self, slice,
        priv->current_picture, FALSE);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "Failed to prepare ref pic list");
      return ret;
    }
  }

  return gst_h266_decoder_decode_slice (self);
}

static void
gst_h266_decoder_finish_picture (GstH266Decoder * self,
    GstH266Picture * picture, GstFlowReturn * ret)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (self);
  GstH266DecoderPrivate *priv = self->priv;
  const GstH266SPS *sps = priv->parser->active_sps;

  g_assert (ret != NULL);

  GST_LOG_OBJECT (self, "Finishing picture %p (poc %d), entries in DPB %d",
      picture, picture->pic_order_cnt, gst_h266_dpb_get_size (priv->dpb));

  /* This picture is decode only, drop corresponding frame */
  if (!picture->output_flag) {
    GstVideoCodecFrame *frame = gst_video_decoder_get_frame (decoder,
        GST_CODEC_PICTURE_FRAME_NUMBER (picture));

    gst_video_decoder_release_frame (decoder, frame);
  }

  /* gst_h266_dpb_add() will take care of pic_latency_cnt increment and
   * reference picture marking for this picture */
  gst_h266_dpb_add (priv->dpb, picture);

  /* NOTE: As per C.5.2.2, bumping by dpb_max_dec_pic_buffering_minus1 is
   * applied only for the output and removal of pictures from the DPB before
   * the decoding of the current picture. So pass zero here */
  while (gst_h266_dpb_needs_bump (priv->dpb,
          sps->dpb.max_num_reorder_pics[sps->max_sublayers_minus1],
          priv->SpsMaxLatencyPictures, 0)) {
    GstH266Picture *to_output = gst_h266_dpb_bump (priv->dpb, FALSE);

    /* Something wrong... */
    if (!to_output) {
      GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
      break;
    }

    gst_h266_decoder_do_output_picture (self, to_output, ret);
  }
}

static void
gst_h266_decoder_finish_current_picture (GstH266Decoder * self,
    GstFlowReturn * ret)
{
  GstH266DecoderPrivate *priv = self->priv;
  GstH266DecoderClass *klass;
  GstFlowReturn flow_ret = GST_FLOW_OK;

  g_assert (ret != NULL);

  if (!priv->current_picture)
    return;

  klass = GST_H266_DECODER_GET_CLASS (self);

  if (klass->end_picture) {
    flow_ret = klass->end_picture (self, priv->current_picture);
    if (flow_ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "End picture failed");

      /* continue to empty dpb */
      UPDATE_FLOW_RETURN (ret, flow_ret);
    }
  }

  /* finish picture takes ownership of the picture */
  gst_h266_decoder_finish_picture (self, priv->current_picture, &flow_ret);
  priv->current_picture = NULL;

  UPDATE_FLOW_RETURN (ret, flow_ret);
}

static GstFlowReturn
gst_h266_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstH266Decoder *self = GST_H266_DECODER (decoder);
  GstH266DecoderPrivate *priv = self->priv;
  GstBuffer *in_buf = frame->input_buffer;
  GstH266NalUnit nalu;
  GstH266ParserResult pres;
  GstMapInfo map;
  GstFlowReturn decode_ret = GST_FLOW_OK;
  guint i;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  gst_h266_decoder_reset_frame_state (self);

  priv->last_flow = GST_FLOW_OK;
  priv->current_frame = frame;

  if (!gst_buffer_map (in_buf, &map, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ,
        ("Failed to map memory for reading"), (NULL));
    return GST_FLOW_ERROR;
  }

  if (priv->in_format == GST_H266_DECODER_FORMAT_VVC1 ||
      priv->in_format == GST_H266_DECODER_FORMAT_VVI1) {
    gst_buffer_unmap (in_buf, &map);
    gst_h266_decoder_reset_frame_state (self);
    return GST_FLOW_NOT_SUPPORTED;
  } else {
    pres = gst_h266_parser_identify_nalu (priv->parser,
        map.data, 0, map.size, &nalu);

    /* Should already aligned to AU. */
    if (pres == GST_H266_PARSER_NO_NAL_END)
      pres = GST_H266_PARSER_OK;

    while (pres == GST_H266_PARSER_OK) {
      pres = gst_h266_decoder_parse_nalu (self, &nalu);
      if (pres != GST_H266_PARSER_OK)
        break;

      pres = gst_h266_parser_identify_nalu (priv->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);

      if (pres == GST_H266_PARSER_NO_NAL_END)
        pres = GST_H266_PARSER_OK;
    }
  }

  for (i = 0; i < priv->slices->len && decode_ret == GST_FLOW_OK; i++) {
    GstH266Slice *decoder_slice =
        &g_array_index (priv->slices, GstH266Slice, i);
    if (!gst_h266_decoder_collect_aps_list (self, decoder_slice))
      decode_ret = GST_FLOW_ERROR;
  }

  for (i = 0; i < priv->slices->len && decode_ret == GST_FLOW_OK; i++) {
    GstH266Slice *decoder_slice =
        &g_array_index (priv->slices, GstH266Slice, i);
    decode_ret = gst_h266_decoder_process_slice (self, decoder_slice);
  }

  gst_buffer_unmap (in_buf, &map);
  gst_h266_decoder_reset_frame_state (self);

  if (decode_ret != GST_FLOW_OK) {
    if (decode_ret == GST_FLOW_ERROR) {
      GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
          ("Failed to decode data"), (NULL), decode_ret);
    }

    gst_video_decoder_release_frame (decoder, frame);
    gst_clear_h266_picture (&priv->current_picture);

    return decode_ret;
  }

  if (priv->current_picture) {
    gst_h266_decoder_finish_current_picture (self, &decode_ret);
    gst_video_codec_frame_unref (frame);
  } else {
    /* This picture was dropped */
    gst_video_decoder_release_frame (decoder, frame);
  }

  if (priv->last_flow != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self,
        "Last flow %s", gst_flow_get_name (priv->last_flow));
    return priv->last_flow;
  }

  if (decode_ret == GST_FLOW_ERROR) {
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), decode_ret);
  }

  return decode_ret;
}

static void
gst_h266_decoder_finalize (GObject * object)
{
  GstH266Decoder *self = GST_H266_DECODER (object);
  GstH266DecoderPrivate *priv = self->priv;
  gint i;

  g_array_unref (priv->slices);

  for (i = 0; i < GST_H266_APS_TYPE_MAX; i++)
    g_array_unref (self->aps_list[i]);

  gst_queue_array_free (priv->output_queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h266_decoder_init (GstH266Decoder * self)
{
  GstH266DecoderPrivate *priv;
  gint i;

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (self), TRUE);

  priv = gst_h266_decoder_get_instance_private (self);
  self->priv = priv;

  priv->last_output_poc = G_MININT32;

  priv->slices = g_array_sized_new (FALSE, TRUE, sizeof (GstH266Slice), 8);

  for (i = 0; i < GST_H266_APS_TYPE_MAX; i++)
    self->aps_list[i] = g_array_new (FALSE, TRUE, sizeof (GstH266APS *));

  priv->output_queue =
      gst_queue_array_new_for_struct (sizeof (GstH266DecoderOutputFrame), 1);
  gst_queue_array_set_clear_func (priv->output_queue,
      (GDestroyNotify) gst_h266_decoder_clear_output_frame);
}

static void
gst_h266_decoder_class_init (GstH266DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_h266_decoder_finalize);

  decoder_class->start = GST_DEBUG_FUNCPTR (gst_h266_decoder_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_h266_decoder_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h266_decoder_set_format);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_h266_decoder_negotiate);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_h266_decoder_finish);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_h266_decoder_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_h266_decoder_drain);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h266_decoder_handle_frame);
}
