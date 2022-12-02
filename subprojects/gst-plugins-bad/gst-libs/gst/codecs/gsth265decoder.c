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

  guint8 conformance_window_flag;
  gint crop_rect_width;
  gint crop_rect_height;
  gint crop_rect_x;
  gint crop_rect_y;

  /* input codec_data, if any */
  GstBuffer *codec_data;
  guint nal_length_size;

  /* state */
  GstH265DecoderFormat in_format;
  GstH265DecoderAlign align;
  GstH265Parser *parser;
  GstH265Dpb *dpb;

  /* 0: frame or field-pair interlaced stream
   * 1: alternating, single field interlaced stream.
   * When equal to 1, picture timing SEI shall be present in every AU */
  guint8 field_seq_flag;
  guint8 progressive_source_flag;
  guint8 interlaced_source_flag;

  /* Updated/cleared per handle_frame() by using picture timeing SEI */
  GstH265SEIPicStructType cur_pic_struct;
  guint8 cur_source_scan_type;
  guint8 cur_duplicate_flag;

  /* vps/sps/pps of the current slice */
  const GstH265VPS *active_vps;
  const GstH265SPS *active_sps;
  const GstH265PPS *active_pps;

  guint32 SpsMaxLatencyPictures;

  /* Picture currently being processed/decoded */
  GstH265Picture *current_picture;
  GstVideoCodecFrame *current_frame;

  /* Slice (slice header + nalu) currently being processed/decoded */
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

  /* Reference picture lists, constructed for each slice */
  gboolean process_ref_pic_lists;
  GArray *ref_pic_list_tmp;
  GArray *ref_pic_list0;
  GArray *ref_pic_list1;
};

#define UPDATE_FLOW_RETURN(ret,new_ret) G_STMT_START { \
  if (*(ret) == GST_FLOW_OK) \
    *(ret) = new_ret; \
} G_STMT_END

#define parent_class gst_h265_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH265Decoder, gst_h265_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstH265Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_h265_decoder_debug, "h265decoder", 0,
        "H.265 Video Decoder"));

static void gst_h265_decoder_finalize (GObject * object);

static gboolean gst_h265_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_h265_decoder_stop (GstVideoDecoder * decoder);
static gboolean gst_h265_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_h265_decoder_finish (GstVideoDecoder * decoder);
static gboolean gst_h265_decoder_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_h265_decoder_drain (GstVideoDecoder * decoder);
static GstFlowReturn gst_h265_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static void gst_h265_decoder_finish_current_picture (GstH265Decoder * self,
    GstFlowReturn * ret);
static void gst_h265_decoder_clear_ref_pic_sets (GstH265Decoder * self);
static void gst_h265_decoder_clear_dpb (GstH265Decoder * self, gboolean flush);
static GstFlowReturn gst_h265_decoder_drain_internal (GstH265Decoder * self);
static GstFlowReturn
gst_h265_decoder_start_current_picture (GstH265Decoder * self);

static void
gst_h265_decoder_class_init (GstH265DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_h265_decoder_finalize);

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
  GstH265DecoderPrivate *priv;

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->priv = priv = gst_h265_decoder_get_instance_private (self);

  priv->last_output_poc = G_MININT32;

  priv->ref_pic_list_tmp = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH265Picture *), 32);
  priv->ref_pic_list0 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH265Picture *), 32);
  priv->ref_pic_list1 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH265Picture *), 32);
}

static void
gst_h265_decoder_finalize (GObject * object)
{
  GstH265Decoder *self = GST_H265_DECODER (object);
  GstH265DecoderPrivate *priv = self->priv;

  g_array_unref (priv->ref_pic_list_tmp);
  g_array_unref (priv->ref_pic_list0);
  g_array_unref (priv->ref_pic_list1);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

  gst_h265_decoder_clear_ref_pic_sets (self);

  return TRUE;
}

static GstFlowReturn
gst_h265_decoder_parse_vps (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265VPS vps;
  GstH265ParserResult pres;

  pres = gst_h265_parser_parse_vps (priv->parser, nalu, &vps);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse VPS, result %d", pres);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "VPS parsed");

  return GST_FLOW_OK;
}

static gboolean
gst_h265_decoder_is_crop_rect_changed (GstH265Decoder * self, GstH265SPS * sps)
{
  GstH265DecoderPrivate *priv = self->priv;

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

static GstFlowReturn
gst_h265_decoder_process_sps (GstH265Decoder * self, GstH265SPS * sps)
{
  GstH265DecoderPrivate *priv = self->priv;
  gint max_dpb_size;
  gint prev_max_dpb_size;
  gint MaxLumaPS;
  const gint MaxDpbPicBuf = 6;
  gint PicSizeInSamplesY;
  guint8 field_seq_flag = 0;
  guint8 progressive_source_flag = 0;
  guint8 interlaced_source_flag = 0;
  GstFlowReturn ret = GST_FLOW_OK;

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

  if (sps->vui_parameters_present_flag)
    field_seq_flag = sps->vui_params.field_seq_flag;

  progressive_source_flag = sps->profile_tier_level.progressive_source_flag;
  interlaced_source_flag = sps->profile_tier_level.interlaced_source_flag;

  prev_max_dpb_size = gst_h265_dpb_get_max_num_pics (priv->dpb);
  if (priv->width != sps->width || priv->height != sps->height ||
      prev_max_dpb_size != max_dpb_size ||
      priv->field_seq_flag != field_seq_flag ||
      priv->progressive_source_flag != progressive_source_flag ||
      priv->interlaced_source_flag != interlaced_source_flag ||
      gst_h265_decoder_is_crop_rect_changed (self, sps)) {
    GstH265DecoderClass *klass = GST_H265_DECODER_GET_CLASS (self);

    GST_DEBUG_OBJECT (self,
        "SPS updated, resolution: %dx%d -> %dx%d, dpb size: %d -> %d, "
        "field_seq_flag: %d -> %d, progressive_source_flag: %d -> %d, "
        "interlaced_source_flag: %d -> %d",
        priv->width, priv->height, sps->width, sps->height,
        prev_max_dpb_size, max_dpb_size, priv->field_seq_flag, field_seq_flag,
        priv->progressive_source_flag, progressive_source_flag,
        priv->interlaced_source_flag, interlaced_source_flag);

    g_assert (klass->new_sequence);

    ret = klass->new_sequence (self, sps, max_dpb_size);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "subclass does not want accept new sequence");
      return ret;
    }

    priv->width = sps->width;
    priv->height = sps->height;
    priv->conformance_window_flag = sps->conformance_window_flag;
    priv->crop_rect_width = sps->crop_rect_width;
    priv->crop_rect_height = sps->crop_rect_height;
    priv->crop_rect_x = sps->crop_rect_x;
    priv->crop_rect_y = sps->crop_rect_y;
    priv->field_seq_flag = field_seq_flag;
    priv->progressive_source_flag = progressive_source_flag;
    priv->interlaced_source_flag = interlaced_source_flag;

    gst_h265_dpb_set_max_num_pics (priv->dpb, max_dpb_size);
  }

  if (sps->max_latency_increase_plus1[sps->max_sub_layers_minus1]) {
    priv->SpsMaxLatencyPictures =
        sps->max_num_reorder_pics[sps->max_sub_layers_minus1] +
        sps->max_latency_increase_plus1[sps->max_sub_layers_minus1] - 1;
  }

  GST_DEBUG_OBJECT (self, "Set DPB max size %d", max_dpb_size);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_decoder_parse_sps (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265SPS sps;
  GstH265ParserResult pres;
  GstFlowReturn ret = GST_FLOW_OK;

  pres = gst_h265_parse_sps (priv->parser, nalu, &sps, TRUE);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SPS, result %d", pres);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "SPS parsed");

  ret = gst_h265_decoder_process_sps (self, &sps);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Failed to process SPS");
  } else if (gst_h265_parser_update_sps (priv->parser,
          &sps) != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to update SPS");
    ret = GST_FLOW_ERROR;
  }

  return ret;
}

static GstFlowReturn
gst_h265_decoder_parse_pps (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265PPS pps;
  GstH265ParserResult pres;

  pres = gst_h265_parser_parse_pps (priv->parser, nalu, &pps);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse PPS, result %d", pres);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "PPS parsed");

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_decoder_parse_sei (GstH265Decoder * self, GstH265NalUnit * nalu)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265ParserResult pres;
  GArray *messages = NULL;
  guint i;

  pres = gst_h265_parser_parse_sei (priv->parser, nalu, &messages);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SEI, result %d", pres);

    /* XXX: Ignore error from SEI parsing, it might be malformed bitstream,
     * or our fault. But shouldn't be critical  */
    g_clear_pointer (&messages, g_array_unref);
    return GST_FLOW_OK;
  }

  for (i = 0; i < messages->len; i++) {
    GstH265SEIMessage *sei = &g_array_index (messages, GstH265SEIMessage, i);

    switch (sei->payloadType) {
      case GST_H265_SEI_PIC_TIMING:
        priv->cur_pic_struct = sei->payload.pic_timing.pic_struct;
        priv->cur_source_scan_type = sei->payload.pic_timing.source_scan_type;
        priv->cur_duplicate_flag = sei->payload.pic_timing.duplicate_flag;

        GST_TRACE_OBJECT (self,
            "Picture Timing SEI, pic_struct: %d, source_scan_type: %d, "
            "duplicate_flag: %d", priv->cur_pic_struct,
            priv->cur_source_scan_type, priv->cur_duplicate_flag);
        break;
      default:
        break;
    }
  }

  g_array_free (messages, TRUE);
  GST_LOG_OBJECT (self, "SEI parsed");

  return GST_FLOW_OK;
}

static void
gst_h265_decoder_process_ref_pic_lists (GstH265Decoder * self,
    GstH265Picture * curr_pic, GstH265Slice * slice,
    GArray ** ref_pic_list0, GArray ** ref_pic_list1)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265RefPicListModification *ref_mod =
      &slice->header.ref_pic_list_modification;
  GstH265PPSSccExtensionParams *scc_ext =
      &slice->header.pps->pps_scc_extension_params;
  GArray *tmp_refs;
  gint num_tmp_refs, i;

  *ref_pic_list0 = priv->ref_pic_list0;
  *ref_pic_list1 = priv->ref_pic_list1;

  /* There is nothing to be done for I slices */
  if (GST_H265_IS_I_SLICE (&slice->header))
    return;

  /* Inifinit loop prevention */
  if (self->NumPocStCurrBefore == 0 && self->NumPocStCurrAfter == 0 &&
      self->NumPocLtCurr == 0 && !scc_ext->pps_curr_pic_ref_enabled_flag) {
    GST_WARNING_OBJECT (self,
        "Expected references, got none, preventing infinit loop.");
    return;
  }

  /* 8.3.4 Deriving l0 */
  tmp_refs = priv->ref_pic_list_tmp;

  /* (8-8)
   * Deriving l0 consist of appending in loop RefPicSetStCurrBefore,
   * RefPicSetStCurrAfter and RefPicSetLtCurr until NumRpsCurrTempList0 item
   * has been reached.
   */

  /* NumRpsCurrTempList0 */
  num_tmp_refs = MAX (slice->header.num_ref_idx_l0_active_minus1 + 1,
      self->NumPicTotalCurr);

  while (tmp_refs->len < num_tmp_refs) {
    for (i = 0; i < self->NumPocStCurrBefore && tmp_refs->len < num_tmp_refs;
        i++)
      g_array_append_val (tmp_refs, self->RefPicSetStCurrBefore[i]);
    for (i = 0; i < self->NumPocStCurrAfter && tmp_refs->len < num_tmp_refs;
        i++)
      g_array_append_val (tmp_refs, self->RefPicSetStCurrAfter[i]);
    for (i = 0; i < self->NumPocLtCurr && tmp_refs->len < num_tmp_refs; i++)
      g_array_append_val (tmp_refs, self->RefPicSetLtCurr[i]);
    if (scc_ext->pps_curr_pic_ref_enabled_flag)
      g_array_append_val (tmp_refs, curr_pic);
  }

  /* (8-9)
   * If needed, apply the modificaiton base on the lookup table found in the
   * slice header (list_entry_l0).
   */
  for (i = 0; i <= slice->header.num_ref_idx_l0_active_minus1; i++) {
    GstH265Picture **tmp = (GstH265Picture **) tmp_refs->data;

    if (ref_mod->ref_pic_list_modification_flag_l0)
      g_array_append_val (*ref_pic_list0, tmp[ref_mod->list_entry_l0[i]]);
    else
      g_array_append_val (*ref_pic_list0, tmp[i]);
  }

  if (scc_ext->pps_curr_pic_ref_enabled_flag &&
      !ref_mod->ref_pic_list_modification_flag_l0 &&
      num_tmp_refs > (slice->header.num_ref_idx_l0_active_minus1 + 1)) {
    g_array_index (*ref_pic_list0, GstH265Picture *,
        slice->header.num_ref_idx_l0_active_minus1) = curr_pic;
  }

  g_array_set_size (tmp_refs, 0);

  /* For P slices we only need l0 */
  if (GST_H265_IS_P_SLICE (&slice->header))
    return;

  /* 8.3.4 Deriving l1 */
  /* (8-10)
   * Deriving l1 consist of appending in loop RefPicSetStCurrAfter,
   * RefPicSetStCurrBefore and RefPicSetLtCurr until NumRpsCurrTempList0 item
   * has been reached.
   */

  /* NumRpsCurrTempList1 */
  num_tmp_refs = MAX (slice->header.num_ref_idx_l1_active_minus1 + 1,
      self->NumPicTotalCurr);

  while (tmp_refs->len < num_tmp_refs) {
    for (i = 0; i < self->NumPocStCurrAfter && tmp_refs->len < num_tmp_refs;
        i++)
      g_array_append_val (tmp_refs, self->RefPicSetStCurrAfter[i]);
    for (i = 0; i < self->NumPocStCurrBefore && tmp_refs->len < num_tmp_refs;
        i++)
      g_array_append_val (tmp_refs, self->RefPicSetStCurrBefore[i]);
    for (i = 0; i < self->NumPocLtCurr && tmp_refs->len < num_tmp_refs; i++)
      g_array_append_val (tmp_refs, self->RefPicSetLtCurr[i]);
    if (scc_ext->pps_curr_pic_ref_enabled_flag)
      g_array_append_val (tmp_refs, curr_pic);
  }

  /* (8-11)
   * If needed, apply the modificaiton base on the lookup table found in the
   * slice header (list_entry_l1).
   */
  for (i = 0; i <= slice->header.num_ref_idx_l1_active_minus1; i++) {
    GstH265Picture **tmp = (GstH265Picture **) tmp_refs->data;

    if (ref_mod->ref_pic_list_modification_flag_l1)
      g_array_append_val (*ref_pic_list1, tmp[ref_mod->list_entry_l1[i]]);
    else
      g_array_append_val (*ref_pic_list1, tmp[i]);
  }

  g_array_set_size (tmp_refs, 0);
}

static GstFlowReturn
gst_h265_decoder_decode_slice (GstH265Decoder * self)
{
  GstH265DecoderClass *klass = GST_H265_DECODER_GET_CLASS (self);
  GstH265DecoderPrivate *priv = self->priv;
  GstH265Slice *slice = &priv->current_slice;
  GstH265Picture *picture = priv->current_picture;
  GArray *l0 = NULL;
  GArray *l1 = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    return GST_FLOW_ERROR;
  }

  g_assert (klass->decode_slice);

  if (priv->process_ref_pic_lists) {
    l0 = priv->ref_pic_list0;
    l1 = priv->ref_pic_list1;
    gst_h265_decoder_process_ref_pic_lists (self, picture, slice, &l0, &l1);
  }

  ret = klass->decode_slice (self, picture, slice, l0, l1);

  if (priv->process_ref_pic_lists) {
    g_array_set_size (l0, 0);
    g_array_set_size (l1, 0);
  }

  return ret;
}

static GstFlowReturn
gst_h265_decoder_preprocess_slice (GstH265Decoder * self, GstH265Slice * slice)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SliceHdr *slice_hdr = &slice->header;

  if (priv->current_picture && slice_hdr->first_slice_segment_in_pic_flag) {
    GST_WARNING_OBJECT (self,
        "Current picture is not finished but slice header has "
        "first_slice_segment_in_pic_flag");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_decoder_parse_slice (GstH265Decoder * self, GstH265NalUnit * nalu,
    GstClockTime pts)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265ParserResult pres = GST_H265_PARSER_OK;
  GstFlowReturn ret = GST_FLOW_OK;

  memset (&priv->current_slice, 0, sizeof (GstH265Slice));

  pres = gst_h265_parser_parse_slice_hdr (priv->parser, nalu,
      &priv->current_slice.header);

  if (pres != GST_H265_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to parse slice header, ret %d", pres);
    memset (&priv->current_slice, 0, sizeof (GstH265Slice));

    return GST_FLOW_ERROR;
  }

  /* NOTE: gst_h265_parser_parse_slice_hdr() allocates array
   * GstH265SliceHdr::entry_point_offset_minus1 but we don't use it
   * in this h265decoder baseclass at the moment
   */
  gst_h265_slice_hdr_free (&priv->current_slice.header);

  priv->current_slice.nalu = *nalu;

  if (priv->current_slice.header.dependent_slice_segment_flag) {
    GstH265SliceHdr *slice_hdr = &priv->current_slice.header;
    GstH265SliceHdr *indep_slice_hdr = &priv->prev_independent_slice.header;

    memcpy (&slice_hdr->type, &indep_slice_hdr->type,
        G_STRUCT_OFFSET (GstH265SliceHdr, num_entry_point_offsets) -
        G_STRUCT_OFFSET (GstH265SliceHdr, type));
  } else {
    priv->prev_independent_slice = priv->current_slice;
    memset (&priv->prev_independent_slice.nalu, 0, sizeof (GstH265NalUnit));
  }

  ret = gst_h265_decoder_preprocess_slice (self, &priv->current_slice);
  if (ret != GST_FLOW_OK)
    return ret;

  priv->active_pps = priv->current_slice.header.pps;
  priv->active_sps = priv->active_pps->sps;

  if (!priv->current_picture) {
    GstH265DecoderClass *klass = GST_H265_DECODER_GET_CLASS (self);
    GstH265Picture *picture;
    GstFlowReturn ret = GST_FLOW_OK;

    g_assert (priv->current_frame);

    picture = gst_h265_picture_new ();
    picture->pts = pts;
    /* This allows accessing the frame from the picture. */
    picture->system_frame_number = priv->current_frame->system_frame_number;

    priv->current_picture = picture;

    if (klass->new_picture)
      ret = klass->new_picture (self, priv->current_frame, picture);

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "subclass does not want accept new picture");
      priv->current_picture = NULL;
      gst_h265_picture_unref (picture);
      return ret;
    }

    ret = gst_h265_decoder_start_current_picture (self);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "start picture failed");
      return ret;
    }

    /* this picture was dropped */
    if (!priv->current_picture)
      return GST_FLOW_OK;
  }

  return gst_h265_decoder_decode_slice (self);
}

static GstFlowReturn
gst_h265_decoder_decode_nal (GstH265Decoder * self, GstH265NalUnit * nalu,
    GstClockTime pts)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;

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
    case GST_H265_NAL_PREFIX_SEI:
    case GST_H265_NAL_SUFFIX_SEI:
      ret = gst_h265_decoder_parse_sei (self, nalu);
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
      priv->new_bitstream = TRUE;
      break;
    case GST_H265_NAL_EOS:
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

static GstFlowReturn
gst_h265_decoder_parse_codec_data (GstH265Decoder * self, const guint8 * data,
    gsize size)
{
  GstH265DecoderPrivate *priv = self->priv;
  guint num_nal_arrays;
  guint off;
  guint num_nals, i, j;
  GstH265ParserResult pres;
  GstH265NalUnit nalu;
  GstFlowReturn ret = GST_FLOW_OK;

  /* parse the hvcC data */
  if (size < 23) {
    GST_WARNING_OBJECT (self, "hvcC too small");
    return GST_FLOW_ERROR;
  }

  /* wrong hvcC version */
  if (data[0] != 0 && data[0] != 1) {
    return GST_FLOW_ERROR;
  }

  priv->nal_length_size = (data[21] & 0x03) + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", priv->nal_length_size);

  num_nal_arrays = data[22];
  off = 23;

  for (i = 0; i < num_nal_arrays; i++) {
    if (off + 3 >= size) {
      GST_WARNING_OBJECT (self, "hvcC too small");
      return GST_FLOW_ERROR;
    }

    num_nals = GST_READ_UINT16_BE (data + off + 1);
    off += 3;
    for (j = 0; j < num_nals; j++) {
      pres = gst_h265_parser_identify_nalu_hevc (priv->parser,
          data, off, size, 2, &nalu);

      if (pres != GST_H265_PARSER_OK) {
        GST_WARNING_OBJECT (self, "hvcC too small");
        return GST_FLOW_ERROR;
      }

      switch (nalu.type) {
        case GST_H265_NAL_VPS:
          ret = gst_h265_decoder_parse_vps (self, &nalu);
          if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT (self, "Failed to parse VPS");
            return ret;
          }
          break;
        case GST_H265_NAL_SPS:
          ret = gst_h265_decoder_parse_sps (self, &nalu);
          if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT (self, "Failed to parse SPS");
            return ret;
          }
          break;
        case GST_H265_NAL_PPS:
          ret = gst_h265_decoder_parse_pps (self, &nalu);
          if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT (self, "Failed to parse PPS");
            return ret;
          }
          break;
        default:
          break;
      }

      off = nalu.offset + nalu.size;
    }
  }

  return GST_FLOW_OK;
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
    if (gst_h265_decoder_parse_codec_data (self, map.data, map.size) !=
        GST_FLOW_OK) {
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

  gst_h265_decoder_clear_dpb (self, TRUE);

  return TRUE;
}

static GstFlowReturn
gst_h265_decoder_drain (GstVideoDecoder * decoder)
{
  GstH265Decoder *self = GST_H265_DECODER (decoder);

  /* dpb will be cleared by this method */
  return gst_h265_decoder_drain_internal (self);
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
gst_h265_decoder_set_buffer_flags (GstH265Decoder * self,
    GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;

  switch (picture->pic_struct) {
    case GST_H265_SEI_PIC_STRUCT_FRAME:
      break;
    case GST_H265_SEI_PIC_STRUCT_TOP_FIELD:
    case GST_H265_SEI_PIC_STRUCT_TOP_PAIRED_PREVIOUS_BOTTOM:
    case GST_H265_SEI_PIC_STRUCT_TOP_PAIRED_NEXT_BOTTOM:
      if (!priv->field_seq_flag) {
        GST_FIXME_OBJECT (self,
            "top-field with field_seq_flag == 0, what does it mean?");
      } else {
        picture->buffer_flags = GST_VIDEO_BUFFER_FLAG_TOP_FIELD;
      }
      break;
    case GST_H265_SEI_PIC_STRUCT_BOTTOM_FIELD:
    case GST_H265_SEI_PIC_STRUCT_BOTTOM_PAIRED_PREVIOUS_TOP:
    case GST_H265_SEI_PIC_STRUCT_BOTTOM_PAIRED_NEXT_TOP:
      if (!priv->field_seq_flag) {
        GST_FIXME_OBJECT (self,
            "bottom-field with field_seq_flag == 0, what does it mean?");
      } else {
        picture->buffer_flags = GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD;
      }
      break;
    case GST_H265_SEI_PIC_STRUCT_TOP_BOTTOM:
      if (priv->field_seq_flag) {
        GST_FIXME_OBJECT (self,
            "TFF with field_seq_flag == 1, what does it mean?");
      } else {
        picture->buffer_flags =
            GST_VIDEO_BUFFER_FLAG_INTERLACED | GST_VIDEO_BUFFER_FLAG_TFF;
      }
      break;
    case GST_H265_SEI_PIC_STRUCT_BOTTOM_TOP:
      if (priv->field_seq_flag) {
        GST_FIXME_OBJECT (self,
            "BFF with field_seq_flag == 1, what does it mean?");
      } else {
        picture->buffer_flags = GST_VIDEO_BUFFER_FLAG_INTERLACED;
      }
      break;
    default:
      GST_FIXME_OBJECT (self, "Unhandled picture time SEI pic_struct %d",
          picture->pic_struct);
      break;
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

  /* Use picture struct parsed from picture timing SEI */
  priv->current_picture->pic_struct = priv->cur_pic_struct;
  priv->current_picture->source_scan_type = priv->cur_source_scan_type;
  priv->current_picture->duplicate_flag = priv->cur_duplicate_flag;
  gst_h265_decoder_set_buffer_flags (self, priv->current_picture);

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
gst_h265_decoder_clear_ref_pic_sets (GstH265Decoder * self)
{
  guint i;

  for (i = 0; i < 16; i++) {
    gst_h265_picture_replace (&self->RefPicSetLtCurr[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetLtFoll[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetStCurrBefore[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetStCurrAfter[i], NULL);
    gst_h265_picture_replace (&self->RefPicSetStFoll[i], NULL);
  }
}

static void
gst_h265_decoder_derive_and_mark_rps (GstH265Decoder * self,
    GstH265Picture * picture, gint32 * CurrDeltaPocMsbPresentFlag,
    gint32 * FollDeltaPocMsbPresentFlag)
{
  GstH265DecoderPrivate *priv = self->priv;
  guint i;
  GArray *dpb_array;

  gst_h265_decoder_clear_ref_pic_sets (self);

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

    if (stRefPic == NULL) {
      return FALSE;
    }

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

    self->NumPicTotalCurr = numtotalcurr;

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
  GST_LOG_OBJECT (self, "NumPicTotalCurr:    %d", self->NumPicTotalCurr);

  /* the derivation process for the RPS and the picture marking */
  gst_h265_decoder_derive_and_mark_rps (self, picture,
      CurrDeltaPocMsbPresentFlag, FollDeltaPocMsbPresentFlag);

  return TRUE;
}

static void
gst_h265_decoder_do_output_picture (GstH265Decoder * self,
    GstH265Picture * picture, GstFlowReturn * ret)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265DecoderClass *klass;
  GstVideoCodecFrame *frame = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;

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
      picture->system_frame_number);

  if (!frame) {
    GST_ERROR_OBJECT (self,
        "No available codec frame with frame number %d",
        picture->system_frame_number);
    UPDATE_FLOW_RETURN (ret, GST_FLOW_ERROR);

    gst_h265_picture_unref (picture);
    return;
  }

  klass = GST_H265_DECODER_GET_CLASS (self);

  g_assert (klass->output_picture);
  flow_ret = klass->output_picture (self, frame, picture);

  UPDATE_FLOW_RETURN (ret, flow_ret);
}

static void
gst_h265_decoder_clear_dpb (GstH265Decoder * self, gboolean flush)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (self);
  GstH265DecoderPrivate *priv = self->priv;
  GstH265Picture *picture;

  /* If we are not flushing now, videodecoder baseclass will hold
   * GstVideoCodecFrame. Release frames manually */
  if (!flush) {
    while ((picture = gst_h265_dpb_bump (priv->dpb, TRUE)) != NULL) {
      GstVideoCodecFrame *frame = gst_video_decoder_get_frame (decoder,
          picture->system_frame_number);

      if (frame)
        gst_video_decoder_release_frame (decoder, frame);
      gst_h265_picture_unref (picture);
    }
  }

  gst_h265_dpb_clear (priv->dpb);
  priv->last_output_poc = G_MININT32;
}

static GstFlowReturn
gst_h265_decoder_drain_internal (GstH265Decoder * self)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265Picture *picture;
  GstFlowReturn ret = GST_FLOW_OK;

  while ((picture = gst_h265_dpb_bump (priv->dpb, TRUE)) != NULL)
    gst_h265_decoder_do_output_picture (self, picture, &ret);

  gst_h265_dpb_clear (priv->dpb);
  priv->last_output_poc = G_MININT32;

  return ret;
}

/* C.5.2.2 */
static GstFlowReturn
gst_h265_decoder_dpb_init (GstH265Decoder * self, const GstH265Slice * slice,
    GstH265Picture * picture)
{
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;
  const GstH265SPS *sps = priv->active_sps;
  GstH265Picture *to_output;
  GstFlowReturn ret = GST_FLOW_OK;

  /* C 3.2 */
  if (GST_H265_IS_NAL_TYPE_IRAP (nalu->type) && picture->NoRaslOutputFlag
      && !priv->new_bitstream) {
    if (nalu->type == GST_H265_NAL_SLICE_CRA_NUT)
      picture->NoOutputOfPriorPicsFlag = TRUE;
    else
      picture->NoOutputOfPriorPicsFlag =
          slice_hdr->no_output_of_prior_pics_flag;

    if (picture->NoOutputOfPriorPicsFlag) {
      GST_DEBUG_OBJECT (self, "Clear dpb");
      gst_h265_decoder_clear_dpb (self, FALSE);
    } else {
      gst_h265_dpb_delete_unused (priv->dpb);
      while ((to_output = gst_h265_dpb_bump (priv->dpb, FALSE)) != NULL)
        gst_h265_decoder_do_output_picture (self, to_output, &ret);

      if (gst_h265_dpb_get_size (priv->dpb) > 0) {
        GST_WARNING_OBJECT (self, "IDR or BLA frame failed to clear the dpb, "
            "there are still %d pictures in the dpb, last output poc is %d",
            gst_h265_dpb_get_size (priv->dpb), priv->last_output_poc);
      } else {
        priv->last_output_poc = G_MININT32;
      }
    }
  } else {
    gst_h265_dpb_delete_unused (priv->dpb);
    while (gst_h265_dpb_needs_bump (priv->dpb,
            sps->max_num_reorder_pics[sps->max_sub_layers_minus1],
            priv->SpsMaxLatencyPictures,
            sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1] +
            1)) {
      to_output = gst_h265_dpb_bump (priv->dpb, FALSE);

      /* Something wrong... */
      if (!to_output) {
        GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
        break;
      }

      gst_h265_decoder_do_output_picture (self, to_output, &ret);
    }
  }

  return ret;
}

static GstFlowReturn
gst_h265_decoder_start_current_picture (GstH265Decoder * self)
{
  GstH265DecoderClass *klass;
  GstH265DecoderPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;

  g_assert (priv->current_picture != NULL);
  g_assert (priv->active_sps != NULL);
  g_assert (priv->active_pps != NULL);

  if (!gst_h265_decoder_init_current_picture (self))
    return GST_FLOW_ERROR;

  /* Drop all RASL pictures having NoRaslOutputFlag is TRUE for the
   * associated IRAP picture */
  if (GST_H265_IS_NAL_TYPE_RASL (priv->current_slice.nalu.type) &&
      priv->associated_irap_NoRaslOutputFlag) {
    GST_DEBUG_OBJECT (self, "Drop current picture");
    gst_h265_picture_replace (&priv->current_picture, NULL);
    return GST_FLOW_OK;
  }

  if (!gst_h265_decoder_prepare_rps (self, &priv->current_slice,
          priv->current_picture)) {
    GST_WARNING_OBJECT (self, "Failed to prepare ref pic set");
    return GST_FLOW_ERROR;
  }

  ret = gst_h265_decoder_dpb_init (self,
      &priv->current_slice, priv->current_picture);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Failed to init dpb");
    return ret;
  }

  klass = GST_H265_DECODER_GET_CLASS (self);
  if (klass->start_picture) {
    ret = klass->start_picture (self, priv->current_picture,
        &priv->current_slice, priv->dpb);

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "subclass does not want to start picture");
      return ret;
    }
  }

  return GST_FLOW_OK;
}

static void
gst_h265_decoder_finish_picture (GstH265Decoder * self,
    GstH265Picture * picture, GstFlowReturn * ret)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (self);
  GstH265DecoderPrivate *priv = self->priv;
  const GstH265SPS *sps = priv->active_sps;

  g_assert (ret != NULL);

  GST_LOG_OBJECT (self,
      "Finishing picture %p (poc %d), entries in DPB %d",
      picture, picture->pic_order_cnt, gst_h265_dpb_get_size (priv->dpb));

  gst_h265_dpb_delete_unused (priv->dpb);

  /* This picture is decode only, drop corresponding frame */
  if (!picture->output_flag) {
    GstVideoCodecFrame *frame = gst_video_decoder_get_frame (decoder,
        picture->system_frame_number);

    gst_video_decoder_release_frame (decoder, frame);
  }

  /* gst_h265_dpb_add() will take care of pic_latency_cnt increment and
   * reference picture marking for this picture */
  gst_h265_dpb_add (priv->dpb, picture);

  /* NOTE: As per C.5.2.2, bumping by sps_max_dec_pic_buffering_minus1 is
   * applied only for the output and removal of pictures from the DPB before
   * the decoding of the current picture. So pass zero here */
  while (gst_h265_dpb_needs_bump (priv->dpb,
          sps->max_num_reorder_pics[sps->max_sub_layers_minus1],
          priv->SpsMaxLatencyPictures, 0)) {
    GstH265Picture *to_output = gst_h265_dpb_bump (priv->dpb, FALSE);

    /* Something wrong... */
    if (!to_output) {
      GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
      break;
    }

    gst_h265_decoder_do_output_picture (self, to_output, ret);
  }
}

static void
gst_h265_decoder_finish_current_picture (GstH265Decoder * self,
    GstFlowReturn * ret)
{
  GstH265DecoderPrivate *priv = self->priv;
  GstH265DecoderClass *klass;
  GstFlowReturn flow_ret = GST_FLOW_OK;

  g_assert (ret != NULL);

  if (!priv->current_picture)
    return;

  klass = GST_H265_DECODER_GET_CLASS (self);

  if (klass->end_picture) {
    flow_ret = klass->end_picture (self, priv->current_picture);
    if (flow_ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "End picture failed");

      /* continue to empty dpb */
      UPDATE_FLOW_RETURN (ret, flow_ret);
    }
  }

  /* finish picture takes ownership of the picture */
  gst_h265_decoder_finish_picture (self, priv->current_picture, &flow_ret);
  priv->current_picture = NULL;

  UPDATE_FLOW_RETURN (ret, flow_ret);
}

static void
gst_h265_decoder_reset_frame_state (GstH265Decoder * self)
{
  GstH265DecoderPrivate *priv = self->priv;

  /* Clear picture struct information */
  priv->cur_pic_struct = GST_H265_SEI_PIC_STRUCT_FRAME;
  priv->cur_source_scan_type = 2;
  priv->cur_duplicate_flag = 0;
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
  GstFlowReturn decode_ret = GST_FLOW_OK;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  priv->current_frame = frame;

  gst_h265_decoder_reset_frame_state (self);

  if (!gst_buffer_map (in_buf, &map, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ,
        ("Failed to map memory for reading"), (NULL));
    return GST_FLOW_ERROR;
  }

  if (priv->in_format == GST_H265_DECODER_FORMAT_HVC1 ||
      priv->in_format == GST_H265_DECODER_FORMAT_HEV1) {
    pres = gst_h265_parser_identify_nalu_hevc (priv->parser,
        map.data, 0, map.size, priv->nal_length_size, &nalu);

    while (pres == GST_H265_PARSER_OK && decode_ret == GST_FLOW_OK) {
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

    while (pres == GST_H265_PARSER_OK && decode_ret == GST_FLOW_OK) {
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

  if (decode_ret != GST_FLOW_OK) {
    if (decode_ret == GST_FLOW_ERROR) {
      GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
          ("Failed to decode data"), (NULL), decode_ret);
    }

    gst_video_decoder_drop_frame (decoder, frame);
    gst_h265_picture_clear (&priv->current_picture);

    return decode_ret;
  }

  if (priv->current_picture) {
    gst_h265_decoder_finish_current_picture (self, &decode_ret);
    gst_video_codec_frame_unref (frame);
  } else {
    /* This picture was dropped */
    gst_video_decoder_release_frame (decoder, frame);
  }

  if (decode_ret == GST_FLOW_ERROR) {
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), decode_ret);
  }

  return decode_ret;
}

/**
 * gst_h265_decoder_set_process_ref_pic_lists:
 * @decoder: a #GstH265Decoder
 * @process: whether subclass is requiring reference picture modification process
 *
 * Called to en/disable reference picture modification process.
 *
 * Since: 1.20
 */
void
gst_h265_decoder_set_process_ref_pic_lists (GstH265Decoder * decoder,
    gboolean process)
{
  decoder->priv->process_ref_pic_lists = process;
}

/**
 * gst_h265_decoder_get_picture:
 * @decoder: a #GstH265Decoder
 * @system_frame_number: a target system frame number of #GstH265Picture
 *
 * Retrive DPB and return a #GstH265Picture corresponding to
 * the @system_frame_number
 *
 * Returns: (transfer full): a #GstH265Picture if successful, or %NULL otherwise
 *
 * Since: 1.20
 */
GstH265Picture *
gst_h265_decoder_get_picture (GstH265Decoder * decoder,
    guint32 system_frame_number)
{
  return gst_h265_dpb_get_picture (decoder->priv->dpb, system_frame_number);
}
