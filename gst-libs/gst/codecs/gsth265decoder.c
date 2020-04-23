/* GStreamer
 * Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
 * SECTION:gsth265decoder
 * @title: GstH265Decoder
 * @short_description: Base class to implement stateless H.265 decoders
 * @sources:
 * - gsth265picture.h
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gsth265decoder.h"

GST_DEBUG_CATEGORY (gst_h265_decoder_debug);
#define GST_CAT_DEFAULT gst_h265_decoder_debug

typedef enum
{
  GST_H265_DECODER_FORMAT_NONE,
  GST_H265_DECODER_FORMAT_HVC1,
  GST_H265_DECODER_FORMAT_HEV1,
  GST_H265_DECODER_FORMAT_BYTE
} GstH265DecoderFormat;

typedef enum
{
  GST_H265_DECODER_ALIGN_NONE,
  GST_H265_DECODER_ALIGN_NAL,
  GST_H265_DECODER_ALIGN_AU
} GstH265DecoderAlign;

struct _GstH265DecoderPrivate
{
  gint width, height;

  /* input codec_data, if any */
  GstBuffer *codec_data;
  guint nal_length_size;

  /* state */
  GstH265DecoderFormat in_format;
  GstH265DecoderAlign align;
  GstH265Parser *parser;
  GstH265Dpb *dpb;
  GstFlowReturn last_ret;

  /* vps/sps/pps of the current slice */
  const GstH265VPS *active_vps;
  const GstH265SPS *active_sps;
  const GstH265PPS *active_pps;

  guint32 SpsMaxLatencyPictures;
  gint32 WpOffsetHalfRangeC;

  /* Picture currently being processed/decoded */
  GstH265Picture *current_picture;
  GstVideoCodecFrame *current_frame;

  /* Slice (slice header + nalu) currently being processed/decodec */
  GstH265Slice current_slice;
  GstH265Slice prev_slice;
  GstH265Slice prev_independent_slice;

  gint32 poc;                   // PicOrderCntVal
  gint32 poc_msb;               // PicOrderCntMsb
  gint32 poc_lsb;               // pic_order_cnt_lsb (from slice_header())
  gint32 prev_poc_msb;          // prevPicOrderCntMsb
  gint32 prev_poc_lsb;          // prevPicOrderCntLsb
  gint32 prev_tid0pic_poc_lsb;
  gint32 prev_tid0pic_poc_msb;
  gint32 PocStCurrBefore[16];
  gint32 PocStCurrAfter[16];
  gint32 PocStFoll[16];
  gint32 PocLtCurr[16];
  gint32 PocLtFoll[16];

  /* PicOrderCount of the previously outputted frame */
  gint last_output_poc;

  gboolean associated_irap_NoRaslOutputFlag;
  gboolean new_bitstream;
  gboolean prev_nal_is_eos;
};

#define parent_class gst_h265_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH265Decoder, gst_h265_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstH265Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_h265_decoder_debug, "h265decoder", 0,
        "H.265 Video Decoder"));

static gboolean gst_h265_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_h265_decoder_stop (GstVideoDecoder * decoder);
static gboolean gst_h265_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_h265_decoder_finish (GstVideoDecoder * decoder);
static gboolean gst_h265_decoder_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_h265_decoder_drain (GstVideoDecoder * decoder);
static GstFlowReturn gst_h265_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static gboolean gst_h265_decoder_finish_current_picture (GstH265Decoder * self);
static void gst_h265_decoder_clear_dpb (GstH265Decoder * self);
static gboolean
gst_h265_decoder_output_all_remaining_pics (GstH265Decoder * self);
static gboolean gst_h265_decoder_start_current_picture (GstH265Decoder * self);

static void
gst_h265_decoder_class_init (GstH265DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  decoder_class->start = GST_DEBUG_FUNCPTR (gst_h265_decoder_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_h265_decoder_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h265_decoder_set_format);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_h265_decoder_finish);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_h265_decoder_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_h265_decoder_drain);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h265_decoder_handle_frame);
}

static void
gst_h265_decoder_init (GstH265Decoder * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->priv = gst_h265_decoder_get_instance_private (self);
}

static gboolean
gst_h265_decoder_start (GstVideoDecoder * decoder)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);
  GstH265DecoderPrivate *priv = self->priv;

  priv->parser = gst_h265_parser_new ();
  priv->dpb = gst_h265_dpb_new ();
  priv->new_bitstream = TRUE;
  priv->prev_nal_is_eos = FALSE;

  return TRUE;
}

static gboolean
gst_h265_decoder_stop (GstVideoDecoder * decoder)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);
  GstH265DecoderPrivate *priv = self->priv;

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  gst_clear_buffer (&priv->codec_data);

  if (priv->parser) {
    gst_h265_parser_free (priv->parser);
    priv->parser = NULL;
  }

  if (priv->dpb) {
    gst_h265_dpb_free (priv->dpb);
    priv->dpb = NULL;
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_parse_vps (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265VPS vps;
  GstH265ParserResult pres;
  gboolean ret = TRUE;

  pres = gst_h265_parser_parse_vps (priv->parser, nalu, &vps);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse VPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "VPS parsed");

  return ret;
}

static gboolean
gst_h265_decoder_process_sps (GstH265Decoder * self, GstH265SPS * sps)
{
  GstH265DecoderPrivate *priv = self->priv;
  gint max_dpb_size;
  gint prev_max_dpb_size;
  gint MaxLumaPS;
  const gint MaxDpbPicBuf = 6;
  gint PicSizeInSamplesY;
  guint high_precision_offsets_enabled_flag = 0;
  guint bitdepthC = 0;

  /* A.4.1 */
  MaxLumaPS = 35651584;
  PicSizeInSamplesY = sps->width * sps->height;
  if (PicSizeInSamplesY <= (MaxLumaPS >> 2))
    max_dpb_size = MaxDpbPicBuf * 4;
  else if (PicSizeInSamplesY <= (MaxLumaPS >> 1))
    max_dpb_size = MaxDpbPicBuf * 2;
  else if (PicSizeInSamplesY <= ((3 * MaxLumaPS) >> 2))
    max_dpb_size = (MaxDpbPicBuf * 4) / 3;
  else
    max_dpb_size = MaxDpbPicBuf;

  max_dpb_size = MIN (max_dpb_size, 16);

  prev_max_dpb_size = gst_h265_dpb_get_max_num_pics (priv->dpb);
  if (priv->width != sps->width || priv->height != sps->height ||
      prev_max_dpb_size != max_dpb_size) {
    GstH265DecoderClass *klass = GST_H265_DECODER_GET_CLASS (self);

    GST_DEBUG_OBJECT (self,
        "SPS updated, resolution: %dx%d -> %dx%d, dpb size: %d -> %d",
        priv->width, priv->height, sps->width, sps->height,
        prev_max_dpb_size, max_dpb_size);

    g_assert (klass->new_sequence);

    if (!klass->new_sequence (self, sps, max_dpb_size)) {
      GST_ERROR_OBJECT (self, "subclass does not want accept new sequence");
      return FALSE;
    }

    priv->width = sps->width;
    priv->height = sps->height;

    gst_h265_dpb_set_max_num_pics (priv->dpb, max_dpb_size);
  }

  if (sps->max_latency_increase_plus1[sps->max_sub_layers_minus1]) {
    priv->SpsMaxLatencyPictures =
        sps->max_num_reorder_pics[sps->max_sub_layers_minus1] +
        sps->max_latency_increase_plus1[sps->max_sub_layers_minus1] - 1;
  }

  /* Calculate WpOffsetHalfRangeC: (7-34)
   * FIXME: We don't have parser API for sps_range_extension, so
   * assuming high_precision_offsets_enabled_flag as zero */
  bitdepthC = sps->bit_depth_chroma_minus8 + 8;
  priv->WpOffsetHalfRangeC =
      1 << (high_precision_offsets_enabled_flag ? (bitdepthC - 1) : 7);

  GST_DEBUG_OBJECT (self, "Set DPB max size %d", max_dpb_size);

  return TRUE;
}

static gboolean
gst_h265_decoder_parse_sps (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265SPS sps;
  GstH265ParserResult pres;
  gboolean ret;

  pres = gst_h265_parse_sps (priv->parser, nalu, &sps, TRUE);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "SPS parsed");

  ret = gst_h265_decoder_process_sps (self, &sps);
  if (!ret) {
    GST_WARNING_OBJECT (self, "Failed to process SPS");
  } else if (gst_h265_parser_update_sps (priv->parser,
          &sps) != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to update SPS");
    ret = FALSE;
  }

  return ret;
}

static gboolean
gst_h265_decoder_parse_pps (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265PPS pps;
  GstH265ParserResult pres;

  pres = gst_h265_parser_parse_pps (priv->parser, nalu, &pps);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse PPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "PPS parsed");

  return TRUE;
}

static gboolean
gst_h265_decoder_decode_slice (GstH265Decoder * self)
{
  GstH265DecoderClass *klass = GST_H265_DECODER_GET_CLASS (self);
  GstH265DecoderPrivate *priv = self->priv;
  GstH265Slice *slice = &priv->current_slice;
  GstH265Picture *picture = priv->current_picture;

  if (!picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    return FALSE;
  }

  g_assert (klass->decode_slice);

  return klass->decode_slice (self, picture, slice);
}

static gboolean
gst_h265_decoder_preprocess_slice (GstH265Decoder * self, GstH265Slice * slice)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;

  if (priv->current_picture && slice_hdr->first_slice_segment_in_pic_flag) {
    GST_WARNING_OBJECT (self,
        "Current picture is not finished but slice header has "
        "first_slice_segment_in_pic_flag");
    return FALSE;
  }

  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type)) {
    GST_DEBUG_OBJECT (self, "IDR nalu, clear dpb");
    gst_h265_decoder_drain (GST_VIDEO_DECODER (self));
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_parse_slice (GstH265Decoder * self, GstH265NalUnit * nalu,
    GstClockTime pts)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265ParserResult pres = GST_H265_PARSER_OK;

  memset (&priv->current_slice, 0, sizeof (GstH265Slice));

  pres = gst_h265_parser_parse_slice_hdr (priv->parser, nalu,
      &priv->current_slice.header);

  if (pres != GST_H265_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to parse slice header, ret %d", pres);
    memset (&priv->current_slice, 0, sizeof (GstH265Slice));

    return FALSE;
  }

  priv->current_slice.nalu = *nalu;

  if (!gst_h265_decoder_preprocess_slice (self, &priv->current_slice))
    return FALSE;

  priv->active_pps = priv->current_slice.header.pps;
  priv->active_sps = priv->active_pps->sps;

  if (!priv->current_picture) {
    GstH265DecoderClass *klass = GST_H265_DECODER_GET_CLASS (self);
    GstH265Picture *picture;
    gboolean ret = TRUE;

    picture = gst_h265_picture_new ();
    picture->pts = pts;
    /* This allows accessing the frame from the picture. */
    picture->system_frame_number = priv->current_frame->system_frame_number;

    if (klass->new_picture)
      ret = klass->new_picture (self, picture);

    if (!ret) {
      GST_ERROR_OBJECT (self, "subclass does not want accept new picture");
      gst_h265_picture_unref (picture);
      return FALSE;
    }

    priv->current_picture = picture;
    gst_video_codec_frame_set_user_data (priv->current_frame,
        gst_h265_picture_ref (priv->current_picture),
        (GDestroyNotify) gst_h265_picture_unref);

    if (!gst_h265_decoder_start_current_picture (self)) {
      GST_ERROR_OBJECT (self, "start picture failed");
      return FALSE;
    }

    /* this picture was dropped */
    if (!priv->current_picture)
      return TRUE;
  }

  return gst_h265_decoder_decode_slice (self);
}

static GstFlowReturn
gst_h265_decoder_decode_nal (GstH265Decoder * self, GstH265NalUnit * nalu,
    GstClockTime pts)
{
  GstH265DecoderPrivate *priv = self->priv;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (self, "Parsed nal type: %d, offset %d, size %d",
      nalu->type, nalu->offset, nalu->size);

  switch (nalu->type) {
    case GST_H265_NAL_VPS:
      ret = gst_h265_decoder_parse_vps (self, nalu);
      break;
    case GST_H265_NAL_SPS:
      ret = gst_h265_decoder_parse_sps (self, nalu);
      break;
    case GST_H265_NAL_PPS:
      ret = gst_h265_decoder_parse_pps (self, nalu);
      break;
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TRAIL_R:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_TSA_R:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_STSA_R:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RADL_R:
    case GST_H265_NAL_SLICE_RASL_N:
    case GST_H265_NAL_SLICE_RASL_R:
    case GST_H265_NAL_SLICE_BLA_W_LP:
    case GST_H265_NAL_SLICE_BLA_W_RADL:
    case GST_H265_NAL_SLICE_BLA_N_LP:
    case GST_H265_NAL_SLICE_IDR_W_RADL:
    case GST_H265_NAL_SLICE_IDR_N_LP:
    case GST_H265_NAL_SLICE_CRA_NUT:
      ret = gst_h265_decoder_parse_slice (self, nalu, pts);
      priv->new_bitstream = FALSE;
      priv->prev_nal_is_eos = FALSE;
      break;
    case GST_H265_NAL_EOB:
      gst_h265_decoder_drain (GST_VIDEO_DECODER (self));
      priv->new_bitstream = TRUE;
      break;
    case GST_H265_NAL_EOS:
      gst_h265_decoder_drain (GST_VIDEO_DECODER (self));
      priv->prev_nal_is_eos = TRUE;
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_h265_decoder_format_from_caps (GstH265Decoder * self, GstCaps * caps,
    GstH265DecoderFormat * format, GstH265DecoderAlign * align)
{
  if (format)
    *format = GST_H265_DECODER_FORMAT_NONE;

  if (align)
    *align = GST_H265_DECODER_ALIGN_NONE;

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
        if (strcmp (str, "hvc1") == 0)
          *format = GST_H265_DECODER_FORMAT_HVC1;
        else if (strcmp (str, "hev1") == 0)
          *format = GST_H265_DECODER_FORMAT_HEV1;
        else if (strcmp (str, "byte-stream") == 0)
          *format = GST_H265_DECODER_FORMAT_BYTE;
      }
    }

    if (align) {
      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0)
          *align = GST_H265_DECODER_ALIGN_AU;
        else if (strcmp (str, "nal") == 0)
          *align = GST_H265_DECODER_ALIGN_NAL;
      }
    }
  }
}

static gboolean
gst_h265_decoder_parse_codec_data (GstH265Decoder * self, const guint8 * data,
    gsize size)
{
  GstH265DecoderPrivate *priv = self->priv;
  guint num_nal_arrays;
  guint off;
  guint num_nals, i, j;
  GstH265ParserResult pres;
  GstH265NalUnit nalu;

  /* parse the hvcC data */
  if (size < 23) {
    GST_WARNING_OBJECT (self, "hvcC too small");
    return FALSE;
  }

  /* wrong hvcC version */
  if (data[0] != 0 && data[0] != 1) {
    return FALSE;
  }

  priv->nal_length_size = (data[21] & 0x03) + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", priv->nal_length_size);

  num_nal_arrays = data[22];
  off = 23;

  for (i = 0; i < num_nal_arrays; i++) {
    if (off + 3 >= size) {
      GST_WARNING_OBJECT (self, "hvcC too small");
      return FALSE;
    }

    num_nals = GST_READ_UINT16_BE (data + off + 1);
    off += 3;
    for (j = 0; j < num_nals; j++) {
      pres = gst_h265_parser_identify_nalu_hevc (priv->parser,
          data, off, size, 2, &nalu);

      if (pres != GST_H265_PARSER_OK) {
        GST_WARNING_OBJECT (self, "hvcC too small");
        return FALSE;
      }

      switch (nalu.type) {
        case GST_H265_NAL_VPS:
          if (!gst_h265_decoder_parse_vps (self, &nalu)) {
            GST_WARNING_OBJECT (self, "Failed to parse VPS");
            return FALSE;
          }
          break;
        case GST_H265_NAL_SPS:
          if (!gst_h265_decoder_parse_sps (self, &nalu)) {
            GST_WARNING_OBJECT (self, "Failed to parse SPS");
            return FALSE;
          }
          break;
        case GST_H265_NAL_PPS:
          if (!gst_h265_decoder_parse_pps (self, &nalu)) {
            GST_WARNING_OBJECT (self, "Failed to parse PPS");
            return FALSE;
          }
          break;
        default:
          break;
      }

      off = nalu.offset + nalu.size;
    }
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);
  GstH265DecoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (decoder, "Set format");

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);

  self->input_state = gst_video_codec_state_ref (state);

  if (state->caps) {
    GstStructure *str;
    const GValue *codec_data_value;
    GstH265DecoderFormat format;
    GstH265DecoderAlign align;

    gst_h265_decoder_format_from_caps (self, state->caps, &format, &align);

    str = gst_caps_get_structure (state->caps, 0);
    codec_data_value = gst_structure_get_value (str, "codec_data");

    if (GST_VALUE_HOLDS_BUFFER (codec_data_value)) {
      gst_buffer_replace (&priv->codec_data,
          gst_value_get_buffer (codec_data_value));
    } else {
      gst_buffer_replace (&priv->codec_data, NULL);
    }

    if (format == GST_H265_DECODER_FORMAT_NONE) {
      /* codec_data implies packetized */
      if (codec_data_value != NULL) {
        GST_WARNING_OBJECT (self,
            "video/x-h265 caps with codec_data but no stream-format=hev1 or hvc1");
        format = GST_H265_DECODER_FORMAT_HEV1;
      } else {
        /* otherwise assume bytestream input */
        GST_WARNING_OBJECT (self,
            "video/x-h265 caps without codec_data or stream-format");
        format = GST_H265_DECODER_FORMAT_BYTE;
      }
    }

    if (format == GST_H265_DECODER_FORMAT_HEV1 ||
        format == GST_H265_DECODER_FORMAT_HVC1) {
      if (codec_data_value == NULL) {
        /* Try it with size 4 anyway */
        priv->nal_length_size = 4;
        GST_WARNING_OBJECT (self,
            "packetized format without codec data, assuming nal length size is 4");
      }

      /* AVC implies alignment=au */
      if (align == GST_H265_DECODER_ALIGN_NONE)
        align = GST_H265_DECODER_ALIGN_AU;
    }

    if (format == GST_H265_DECODER_FORMAT_BYTE) {
      if (codec_data_value != NULL) {
        GST_WARNING_OBJECT (self, "bytestream with codec data");
      }
    }

    priv->in_format = format;
    priv->align = align;
  }

  if (priv->codec_data) {
    GstMapInfo map;

    gst_buffer_map (priv->codec_data, &map, GST_MAP_READ);
    if (!gst_h265_decoder_parse_codec_data (self, map.data, map.size)) {
      /* keep going without error.
       * Probably inband SPS/PPS might be valid data */
      GST_WARNING_OBJECT (self, "Failed to handle codec data");
    }
    gst_buffer_unmap (priv->codec_data, &map);
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_flush (GstVideoDecoder * decoder)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);

  gst_h265_decoder_clear_dpb (self);

  return TRUE;
}

static GstFlowReturn
gst_h265_decoder_drain (GstVideoDecoder * decoder)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);
  GstH265DecoderPrivate *priv = self->priv;

  priv->last_ret = GST_FLOW_OK;
  gst_h265_decoder_output_all_remaining_pics (self);
  gst_h265_decoder_clear_dpb (self);

  return priv->last_ret;
}

static GstFlowReturn
gst_h265_decoder_finish (GstVideoDecoder * decoder)
{
  return gst_h265_decoder_drain (decoder);
}

static gboolean
gst_h265_decoder_fill_picture_from_slice (GstH265Decoder * self,
    const GstH265Slice * slice, GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;

  if (nalu->type >= GST_H265_NAL_SLICE_BLA_W_LP &&
      nalu->type <= GST_H265_NAL_SLICE_CRA_NUT)
    picture->RapPicFlag = TRUE;

  /* FIXME: Use SEI header values */
  picture->field = GST_H265_PICTURE_FIELD_FRAME;

  /* NoRaslOutputFlag == 1 if the current picture is
   * 1) an IDR picture
   * 2) a BLA picture
   * 3) a CRA picture that is the first access unit in the bitstream
   * 4) first picture that follows an end of sequence NAL unit in decoding order
   * 5) has HandleCraAsBlaFlag == 1 (set by external means, so not considering )
   */
  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type) ||
      GST_H265_IS_NAL_TYPE_BLA (nalu->type) ||
      (GST_H265_IS_NAL_TYPE_CRA (nalu->type) && priv->new_bitstream) ||
      priv->prev_nal_is_eos) {
    picture->NoRaslOutputFlag = TRUE;
  }

  if (GST_H265_IS_NAL_TYPE_IRAP (nalu->type)) {
    picture->IntraPicFlag = TRUE;
    priv->associated_irap_NoRaslOutputFlag = picture->NoRaslOutputFlag;
  }

  if (GST_H265_IS_NAL_TYPE_RASL (nalu->type) &&
      priv->associated_irap_NoRaslOutputFlag) {
    picture->output_flag = FALSE;
  } else {
    picture->output_flag = slice_hdr->pic_output_flag;
  }

  return TRUE;
}

#define RSV_VCL_N10 10
#define RSV_VCL_N12 12
#define RSV_VCL_N14 14

static gboolean
nal_is_ref (guint8 nal_type)
{
  gboolean ret = FALSE;
  switch (nal_type) {
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RASL_N:
    case RSV_VCL_N10:
    case RSV_VCL_N12:
    case RSV_VCL_N14:
      ret = FALSE;
      break;
    default:
      ret = TRUE;
      break;
  }
  return ret;
}

static gboolean
gst_h265_decoder_calculate_poc (GstH265Decoder * self,
    const GstH265Slice * slice, GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;
  const GstH265SPS *sps = priv->active_sps;
  gint32 MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  gboolean is_irap;

  GST_DEBUG_OBJECT (self, "decode PicOrderCntVal");

  priv->prev_poc_lsb = priv->poc_lsb;
  priv->prev_poc_msb = priv->poc_msb;

  is_irap = GST_H265_IS_NAL_TYPE_IRAP (nalu->type);

  if (!(is_irap && picture->NoRaslOutputFlag)) {
    priv->prev_poc_lsb = priv->prev_tid0pic_poc_lsb;
    priv->prev_poc_msb = priv->prev_tid0pic_poc_msb;
  }

  /* Finding PicOrderCntMsb */
  if (is_irap && picture->NoRaslOutputFlag) {
    priv->poc_msb = 0;
  } else {
    /* (8-1) */
    if ((slice_hdr->pic_order_cnt_lsb < priv->prev_poc_lsb) &&
        ((priv->prev_poc_lsb - slice_hdr->pic_order_cnt_lsb) >=
            (MaxPicOrderCntLsb / 2)))
      priv->poc_msb = priv->prev_poc_msb + MaxPicOrderCntLsb;

    else if ((slice_hdr->pic_order_cnt_lsb > priv->prev_poc_lsb) &&
        ((slice_hdr->pic_order_cnt_lsb - priv->prev_poc_lsb) >
            (MaxPicOrderCntLsb / 2)))
      priv->poc_msb = priv->prev_poc_msb - MaxPicOrderCntLsb;

    else
      priv->poc_msb = priv->prev_poc_msb;
  }

  /* (8-2) */
  priv->poc = picture->pic_order_cnt =
      priv->poc_msb + slice_hdr->pic_order_cnt_lsb;
  priv->poc_lsb = picture->pic_order_cnt_lsb = slice_hdr->pic_order_cnt_lsb;

  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type)) {
    picture->pic_order_cnt = 0;
    picture->pic_order_cnt_lsb = 0;
    priv->poc_lsb = 0;
    priv->poc_msb = 0;
    priv->prev_poc_lsb = 0;
    priv->prev_poc_msb = 0;
    priv->prev_tid0pic_poc_lsb = 0;
    priv->prev_tid0pic_poc_msb = 0;
  }

  GST_DEBUG_OBJECT (self,
      "PicOrderCntVal %d, (lsb %d)", picture->pic_order_cnt,
      picture->pic_order_cnt_lsb);

  if (nalu->temporal_id_plus1 == 1 && !GST_H265_IS_NAL_TYPE_RASL (nalu->type) &&
      !GST_H265_IS_NAL_TYPE_RADL (nalu->type) && nal_is_ref (nalu->type)) {
    priv->prev_tid0pic_poc_lsb = slice_hdr->pic_order_cnt_lsb;
    priv->prev_tid0pic_poc_msb = priv->poc_msb;
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_init_current_picture (GstH265Decoder * self)
{
  GstH265DecoderPrivate *priv = self->priv;

  if (!gst_h265_decoder_fill_picture_from_slice (self, &priv->current_slice,
          priv->current_picture)) {
    return FALSE;
  }

  if (!gst_h265_decoder_calculate_poc (self,
          &priv->current_slice, priv->current_picture))
    return FALSE;

  return TRUE;
}

static gboolean
has_entry_in_rps (GstH265Picture * dpb_pic,
    GstH265Picture ** rps_list, guint rps_list_length)
{
  guint i;

  if (!dpb_pic || !rps_list || !rps_list_length)
    return FALSE;

  for (i = 0; i < rps_list_length; i++) {
    if (rps_list[i] && rps_list[i]->pic_order_cnt == dpb_pic->pic_order_cnt)
      return TRUE;
  }
  return FALSE;
}

static void
gst_h265_decoder_derive_and_mark_rps (GstH265Decoder * self,
    GstH265Picture * picture, gint32 * CurrDeltaPocMsbPresentFlag,
    gint32 * FollDeltaPocMsbPresentFlag)
{
  GstH265DecoderPrivate *priv = self->priv;
  guint i;
  GArray *dpb_array;

  for (i = 0; i < 16; i++) {
    gst_h265_picture_replace (&self->RefPicSetLtCurr[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetLtFoll[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetStCurrBefore[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetStCurrAfter[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetStFoll[i], NULL);
  }

  /* (8-6) */
  for (i = 0; i < self->NumPocLtCurr; i++) {
    if (!CurrDeltaPocMsbPresentFlag[i]) {
      self->RefPicSetLtCurr[i] =
          gst_h265_dpb_get_ref_by_poc_lsb (priv->dpb, priv->PocLtCurr[i]);
    } else {
      self->RefPicSetLtCurr[i] =
          gst_h265_dpb_get_ref_by_poc (priv->dpb, priv->PocLtCurr[i]);
    }
  }

  for (i = 0; i < self->NumPocLtFoll; i++) {
    if (!FollDeltaPocMsbPresentFlag[i]) {
      self->RefPicSetLtFoll[i] =
          gst_h265_dpb_get_ref_by_poc_lsb (priv->dpb, priv->PocLtFoll[i]);
    } else {
      self->RefPicSetLtFoll[i] =
          gst_h265_dpb_get_ref_by_poc (priv->dpb, priv->PocLtFoll[i]);
    }
  }

  /* Mark all ref pics in RefPicSetLtCurr and RefPicSetLtFol as long_term_refs */
  for (i = 0; i < self->NumPocLtCurr; i++) {
    if (self->RefPicSetLtCurr[i]) {
      self->RefPicSetLtCurr[i]->ref = TRUE;
      self->RefPicSetLtCurr[i]->long_term = TRUE;
    }
  }

  for (i = 0; i < self->NumPocLtFoll; i++) {
    if (self->RefPicSetLtFoll[i]) {
      self->RefPicSetLtFoll[i]->ref = TRUE;
      self->RefPicSetLtFoll[i]->long_term = TRUE;
    }
  }

  /* (8-7) */
  for (i = 0; i < self->NumPocStCurrBefore; i++) {
    self->RefPicSetStCurrBefore[i] =
        gst_h265_dpb_get_short_ref_by_poc (priv->dpb, priv->PocStCurrBefore[i]);
  }

  for (i = 0; i < self->NumPocStCurrAfter; i++) {
    self->RefPicSetStCurrAfter[i] =
        gst_h265_dpb_get_short_ref_by_poc (priv->dpb, priv->PocStCurrAfter[i]);
  }

  for (i = 0; i < self->NumPocStFoll; i++) {
    self->RefPicSetStFoll[i] =
        gst_h265_dpb_get_short_ref_by_poc (priv->dpb, priv->PocStFoll[i]);
  }

  /* Mark all dpb pics not beloging to RefPicSet*[] as unused for ref */
  dpb_array = gst_h265_dpb_get_pictures_all (priv->dpb);
  for (i = 0; i < dpb_array->len; i++) {
    GstH265Picture *dpb_pic = g_array_index (dpb_array, GstH265Picture *, i);

    if (dpb_pic &&
        !has_entry_in_rps (dpb_pic, self->RefPicSetLtCurr, self->NumPocLtCurr)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetLtFoll,
            self->NumPocLtFoll)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetStCurrAfter,
            self->NumPocStCurrAfter)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetStCurrBefore,
            self->NumPocStCurrBefore)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetStFoll,
            self->NumPocStFoll)) {
      GST_LOG_OBJECT (self, "Mark Picture %p (poc %d) as non-ref", dpb_pic,
          dpb_pic->pic_order_cnt);
      dpb_pic->ref = FALSE;
      dpb_pic->long_term = FALSE;
    }
  }

  g_array_unref (dpb_array);
}

static gboolean
gst_h265_decoder_prepare_rps (GstH265Decoder * self, const GstH265Slice * slice,
    GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  gint32 CurrDeltaPocMsbPresentFlag[16] = { 0, };
  gint32 FollDeltaPocMsbPresentFlag[16] = { 0, };
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;
  const GstH265SPS *sps = priv->active_sps;
  guint32 MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  gint i, j, k;

  /* if it is an irap pic, set all ref pics in dpb as unused for ref */
  if (GST_H265_IS_NAL_TYPE_IRAP (nalu->type) && picture->NoRaslOutputFlag) {
    GST_DEBUG_OBJECT (self, "Mark all pictures in DPB as non-ref");
    gst_h265_dpb_mark_all_non_ref (priv->dpb);
  }

  /* Reset everything for IDR */
  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type)) {
    memset (priv->PocStCurrBefore, 0, sizeof (priv->PocStCurrBefore));
    memset (priv->PocStCurrAfter, 0, sizeof (priv->PocStCurrAfter));
    memset (priv->PocStFoll, 0, sizeof (priv->PocStFoll));
    memset (priv->PocLtCurr, 0, sizeof (priv->PocLtCurr));
    memset (priv->PocLtFoll, 0, sizeof (priv->PocLtFoll));
    self->NumPocStCurrBefore = self->NumPocStCurrAfter = self->NumPocStFoll = 0;
    self->NumPocLtCurr = self->NumPocLtFoll = 0;
  } else {
    const GstH265ShortTermRefPicSet *stRefPic = NULL;
    gint32 num_lt_pics, pocLt;
    gint32 PocLsbLt[16] = { 0, };
    gint32 UsedByCurrPicLt[16] = { 0, };
    gint32 DeltaPocMsbCycleLt[16] = { 0, };
    gint numtotalcurr = 0;

    /* this is based on CurrRpsIdx described in spec */
    if (!slice_hdr->short_term_ref_pic_set_sps_flag)
      stRefPic = &slice_hdr->short_term_ref_pic_sets;
    else if (sps->num_short_term_ref_pic_sets)
      stRefPic =
          &sps->short_term_ref_pic_set[slice_hdr->short_term_ref_pic_set_idx];

    g_assert (stRefPic != NULL);

    GST_LOG_OBJECT (self,
        "NumDeltaPocs: %d, NumNegativePics: %d, NumPositivePics %d",
        stRefPic->NumDeltaPocs, stRefPic->NumNegativePics,
        stRefPic->NumPositivePics);

    for (i = 0, j = 0, k = 0; i < stRefPic->NumNegativePics; i++) {
      if (stRefPic->UsedByCurrPicS0[i]) {
        priv->PocStCurrBefore[j++] =
            picture->pic_order_cnt + stRefPic->DeltaPocS0[i];
        numtotalcurr++;
      } else
        priv->PocStFoll[k++] = picture->pic_order_cnt + stRefPic->DeltaPocS0[i];
    }
    self->NumPocStCurrBefore = j;
    for (i = 0, j = 0; i < stRefPic->NumPositivePics; i++) {
      if (stRefPic->UsedByCurrPicS1[i]) {
        priv->PocStCurrAfter[j++] =
            picture->pic_order_cnt + stRefPic->DeltaPocS1[i];
        numtotalcurr++;
      } else
        priv->PocStFoll[k++] = picture->pic_order_cnt + stRefPic->DeltaPocS1[i];
    }
    self->NumPocStCurrAfter = j;
    self->NumPocStFoll = k;
    num_lt_pics = slice_hdr->num_long_term_sps + slice_hdr->num_long_term_pics;
    /* The variables PocLsbLt[i] and UsedByCurrPicLt[i] are derived as follows: */
    for (i = 0; i < num_lt_pics; i++) {
      if (i < slice_hdr->num_long_term_sps) {
        PocLsbLt[i] = sps->lt_ref_pic_poc_lsb_sps[slice_hdr->lt_idx_sps[i]];
        UsedByCurrPicLt[i] =
            sps->used_by_curr_pic_lt_sps_flag[slice_hdr->lt_idx_sps[i]];
      } else {
        PocLsbLt[i] = slice_hdr->poc_lsb_lt[i];
        UsedByCurrPicLt[i] = slice_hdr->used_by_curr_pic_lt_flag[i];
      }
      if (UsedByCurrPicLt[i])
        numtotalcurr++;
    }

    self->NumPocTotalCurr = numtotalcurr;

    /* The variable DeltaPocMsbCycleLt[i] is derived as follows: (7-38) */
    for (i = 0; i < num_lt_pics; i++) {
      if (i == 0 || i == slice_hdr->num_long_term_sps)
        DeltaPocMsbCycleLt[i] = slice_hdr->delta_poc_msb_cycle_lt[i];
      else
        DeltaPocMsbCycleLt[i] =
            slice_hdr->delta_poc_msb_cycle_lt[i] + DeltaPocMsbCycleLt[i - 1];
    }

    /* (8-5) */
    for (i = 0, j = 0, k = 0; i < num_lt_pics; i++) {
      pocLt = PocLsbLt[i];
      if (slice_hdr->delta_poc_msb_present_flag[i])
        pocLt +=
            picture->pic_order_cnt - DeltaPocMsbCycleLt[i] * MaxPicOrderCntLsb -
            slice_hdr->pic_order_cnt_lsb;
      if (UsedByCurrPicLt[i]) {
        priv->PocLtCurr[j] = pocLt;
        CurrDeltaPocMsbPresentFlag[j++] =
            slice_hdr->delta_poc_msb_present_flag[i];
      } else {
        priv->PocLtFoll[k] = pocLt;
        FollDeltaPocMsbPresentFlag[k++] =
            slice_hdr->delta_poc_msb_present_flag[i];
      }
    }
    self->NumPocLtCurr = j;
    self->NumPocLtFoll = k;
  }

  GST_LOG_OBJECT (self, "NumPocStCurrBefore: %d", self->NumPocStCurrBefore);
  GST_LOG_OBJECT (self, "NumPocStCurrAfter:  %d", self->NumPocStCurrAfter);
  GST_LOG_OBJECT (self, "NumPocStFoll:       %d", self->NumPocStFoll);
  GST_LOG_OBJECT (self, "NumPocLtCurr:       %d", self->NumPocLtCurr);
  GST_LOG_OBJECT (self, "NumPocLtFoll:       %d", self->NumPocLtFoll);
  GST_LOG_OBJECT (self, "NumPocTotalCurr:    %d", self->NumPocTotalCurr);

  /* the derivation process for the RPS and the picture marking */
  gst_h265_decoder_derive_and_mark_rps (self, picture,
      CurrDeltaPocMsbPresentFlag, FollDeltaPocMsbPresentFlag);

  return TRUE;
}

static void
gst_h265_decoder_clear_dpb (GstH265Decoder * self)
{
  GstH265DecoderPrivate *priv = self->priv;

  gst_h265_dpb_clear (priv->dpb);
  priv->last_output_poc = -1;
}

static void
gst_h265_decoder_do_output_picture (GstH265Decoder * self,
    GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265DecoderClass *klass;

  picture->outputted = TRUE;

  if (picture->pic_order_cnt < priv->last_output_poc) {
    GST_WARNING_OBJECT (self,
        "Outputting out of order %d -> %d, likely a broken stream",
        priv->last_output_poc, picture->pic_order_cnt);
  }

  priv->last_output_poc = picture->pic_order_cnt;

  klass = GST_H265_DECODER_GET_CLASS (self);

  g_assert (klass->output_picture);
  priv->last_ret = klass->output_picture (self, picture);
}

static gint
poc_asc_compare (const GstH265Picture * a, const GstH265Picture * b)
{
  return a->pic_order_cnt > b->pic_order_cnt;
}

static gboolean
gst_h265_decoder_output_all_remaining_pics (GstH265Decoder * self)
{
  GstH265DecoderPrivate *priv = self->priv;
  GList *to_output = NULL;
  GList *iter;

  gst_h265_dpb_get_pictures_not_outputted (priv->dpb, &to_output);

  to_output = g_list_sort (to_output, (GCompareFunc) poc_asc_compare);

  for (iter = to_output; iter; iter = g_list_next (iter)) {
    GstH265Picture *picture = (GstH265Picture *) iter->data;

    GST_LOG_OBJECT (self, "Output picture %p (poc %d)", picture,
        picture->pic_order_cnt);
    gst_h265_decoder_do_output_picture (self, picture);
  }

  if (to_output)
    g_list_free_full (to_output, (GDestroyNotify) gst_h265_picture_unref);

  return TRUE;
}

static gboolean
gst_h265_decoder_check_latency_count (GList * list, guint32 max_latency)
{
  GList *iter;

  for (iter = list; iter; iter = g_list_next (iter)) {
    GstH265Picture *pic = (GstH265Picture *) iter->data;
    if (!pic->outputted && pic->pic_latency_cnt >= max_latency)
      return TRUE;
  }

  return FALSE;
}

/* C.5.2.2 */
static gboolean
gst_h265_decoder_dpb_init (GstH265Decoder * self, const GstH265Slice * slice,
    GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;

  if (GST_H265_IS_NAL_TYPE_IRAP (nalu->type) && picture->NoRaslOutputFlag
      && !priv->new_bitstream) {
    if (nalu->type == GST_H265_NAL_SLICE_CRA_NUT)
      picture->NoOutputOfPriorPicsFlag = TRUE;
    else
      picture->NoOutputOfPriorPicsFlag =
          slice_hdr->no_output_of_prior_pics_flag;

    if (picture->NoOutputOfPriorPicsFlag) {
      GST_DEBUG_OBJECT (self, "Clear dpb");
      gst_h265_decoder_drain (GST_VIDEO_DECODER (self));
    }
  } else {
    /* C 3.2 */
    gst_h265_dpb_delete_unused (priv->dpb);
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_start_current_picture (GstH265Decoder * self)
{
  GstH265DecoderClass *klass;
  GstH265DecoderPrivate *priv = self->priv;
  gboolean ret = TRUE;

  g_assert (priv->current_picture != NULL);
  g_assert (priv->active_sps != NULL);
  g_assert (priv->active_pps != NULL);

  if (!gst_h265_decoder_init_current_picture (self))
    return FALSE;

  /* Drop all RASL pictures having NoRaslOutputFlag is TRUE for the
   * associated IRAP picture */
  if (GST_H265_IS_NAL_TYPE_RASL (priv->current_slice.nalu.type) &&
      priv->associated_irap_NoRaslOutputFlag) {
    GST_DEBUG_OBJECT (self, "Drop current picture");
    gst_h265_picture_replace (&priv->current_picture, NULL);
    return TRUE;
  }

  gst_h265_decoder_prepare_rps (self, &priv->current_slice,
      priv->current_picture);

  gst_h265_decoder_dpb_init (self, &priv->current_slice, priv->current_picture);

  klass = GST_H265_DECODER_GET_CLASS (self);
  if (klass->start_picture)
    ret = klass->start_picture (self, priv->current_picture,
        &priv->current_slice, priv->dpb);

  if (!ret) {
    GST_ERROR_OBJECT (self, "subclass does not want to start picture");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h265_decoder_finish_picture (GstH265Decoder * self,
    GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SPS *sps = priv->active_sps;
  GList *not_outputted = NULL;
  guint num_remaining;
  GList *iter;
#ifndef GST_DISABLE_GST_DEBUG
  gint i;
#endif

  GST_LOG_OBJECT (self,
      "Finishing picture %p (poc %d), entries in DPB %d",
      picture, picture->pic_order_cnt, gst_h265_dpb_get_size (priv->dpb));

  /* Get all pictures that haven't been outputted yet */
  gst_h265_dpb_get_pictures_not_outputted (priv->dpb, &not_outputted);

  /* C.5.2.3 */
  if (picture->output_flag) {
    for (iter = not_outputted; iter; iter = g_list_next (iter)) {
      GstH265Picture *other = GST_H265_PICTURE (iter->data);

      if (!other->outputted)
        other->pic_latency_cnt++;
    }

    picture->outputted = FALSE;
    picture->pic_latency_cnt = 0;
  } else {
    picture->outputted = TRUE;
  }

  /* set pic as short_term_ref */
  picture->ref = TRUE;
  picture->long_term = FALSE;

  /* Include the one we've just decoded */
  if (picture->output_flag) {
    not_outputted =
        g_list_append (not_outputted, gst_h265_picture_ref (picture));
  }

  /* Add to dpb and transfer ownership */
  gst_h265_dpb_add (priv->dpb, picture);

  /* for debugging */
#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Before sorting not outputted list");
  i = 0;
  for (iter = not_outputted; iter; iter = g_list_next (iter)) {
    GstH265Picture *tmp = (GstH265Picture *) iter->data;

    GST_TRACE_OBJECT (self,
        "\t%dth picture %p (poc %d)", i, tmp, tmp->pic_order_cnt);
    i++;
  }
#endif

  /* Sort in output order */
  not_outputted = g_list_sort (not_outputted, (GCompareFunc) poc_asc_compare);

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self,
      "After sorting not outputted list in poc ascending order");
  i = 0;
  for (iter = not_outputted; iter; iter = g_list_next (iter)) {
    GstH265Picture *tmp = (GstH265Picture *) iter->data;

    GST_TRACE_OBJECT (self,
        "\t%dth picture %p (poc %d)", i, tmp, tmp->pic_order_cnt);
    i++;
  }
#endif

  /* Try to output as many pictures as we can. A picture can be output,
   * if the number of decoded and not yet outputted pictures that would remain
   * in DPB afterwards would at least be equal to max_num_reorder_frames.
   * If the outputted picture is not a reference picture, it doesn't have
   * to remain in the DPB and can be removed */
  iter = not_outputted;
  num_remaining = g_list_length (not_outputted);

  while (num_remaining > sps->max_num_reorder_pics[sps->max_sub_layers_minus1]
      || (num_remaining &&
          sps->max_latency_increase_plus1[sps->max_sub_layers_minus1] &&
          gst_h265_decoder_check_latency_count (iter,
              priv->SpsMaxLatencyPictures))) {
    GstH265Picture *to_output = GST_H265_PICTURE (iter->data);

    GST_LOG_OBJECT (self,
        "Output picture %p (poc %d)", to_output, to_output->pic_order_cnt);
    gst_h265_decoder_do_output_picture (self, to_output);
    if (!to_output->ref) {
      /* Current picture hasn't been inserted into DPB yet, so don't remove it
       * if we managed to output it immediately */
      gint outputted_poc = to_output->pic_order_cnt;
      if (outputted_poc != picture->pic_order_cnt) {
        GST_LOG_OBJECT (self, "Delete picture %p (poc %d) from DPB",
            to_output, to_output->pic_order_cnt);
        gst_h265_dpb_delete_by_poc (priv->dpb, outputted_poc);
      }
    }

    iter = g_list_next (iter);
    num_remaining--;
  }

  if (not_outputted)
    g_list_free_full (not_outputted, (GDestroyNotify) gst_h265_picture_unref);

  return TRUE;
}

static gboolean
gst_h265_decoder_finish_current_picture (GstH265Decoder * self)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265DecoderClass *klass;
  gboolean ret = TRUE;

  if (!priv->current_picture)
    return TRUE;

  klass = GST_H265_DECODER_GET_CLASS (self);

  if (klass->end_picture)
    ret = klass->end_picture (self, priv->current_picture);

  /* finish picture takes ownership of the picture */
  ret = gst_h265_decoder_finish_picture (self, priv->current_picture);
  priv->current_picture = NULL;

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to finish picture");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_h265_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);
  GstH265DecoderPrivate *priv = self->priv;
  GstBuffer *in_buf = frame->input_buffer;
  GstH265NalUnit nalu;
  GstH265ParserResult pres;
  GstMapInfo map;
  gboolean decode_ret = TRUE;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  priv->current_frame = frame;
  priv->last_ret = GST_FLOW_OK;

  gst_buffer_map (in_buf, &map, GST_MAP_READ);
  if (priv->in_format == GST_H265_DECODER_FORMAT_HVC1 ||
      priv->in_format == GST_H265_DECODER_FORMAT_HEV1) {
    pres = gst_h265_parser_identify_nalu_hevc (priv->parser,
        map.data, 0, map.size, priv->nal_length_size, &nalu);

    while (pres == GST_H265_PARSER_OK && decode_ret) {
      decode_ret = gst_h265_decoder_decode_nal (self,
          &nalu, GST_BUFFER_PTS (in_buf));

      pres = gst_h265_parser_identify_nalu_hevc (priv->parser,
          map.data, nalu.offset + nalu.size, map.size, priv->nal_length_size,
          &nalu);
    }
  } else {
    pres = gst_h265_parser_identify_nalu (priv->parser,
        map.data, 0, map.size, &nalu);

    if (pres == GST_H265_PARSER_NO_NAL_END)
      pres = GST_H265_PARSER_OK;

    while (pres == GST_H265_PARSER_OK && decode_ret) {
      decode_ret = gst_h265_decoder_decode_nal (self,
          &nalu, GST_BUFFER_PTS (in_buf));

      pres = gst_h265_parser_identify_nalu (priv->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);

      if (pres == GST_H265_PARSER_NO_NAL_END)
        pres = GST_H265_PARSER_OK;
    }
  }

  gst_buffer_unmap (in_buf, &map);
  priv->current_frame = NULL;

  if (!decode_ret) {
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), priv->last_ret);
    gst_video_decoder_drop_frame (decoder, frame);

    gst_h265_picture_clear (&priv->current_picture);

    return priv->last_ret;
  }

  gst_h265_decoder_finish_current_picture (self);
  gst_video_codec_frame_unref (frame);

  return priv->last_ret;
}
