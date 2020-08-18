/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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
 * License along with this library; if not, write to the0
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvah264dec.h"

#include <gst/codecs/gsth264decoder.h>

#include <va/va_drmcommon.h>

#include "gstvaallocator.h"
#include "gstvacaps.h"
#include "gstvadecoder.h"
#include "gstvadevice.h"
#include "gstvadisplay_drm.h"
#include "gstvapool.h"
#include "gstvaprofile.h"
#include "gstvautils.h"
#include "gstvavideoformat.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_h264dec_debug);
#define GST_CAT_DEFAULT gst_va_h264dec_debug

#define GST_VA_H264_DEC(obj)           ((GstVaH264Dec *) obj)
#define GST_VA_H264_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaH264DecClass))
#define GST_VA_H264_DEC_CLASS(klass)   ((GstVaH264DecClass *) klass)

typedef struct _GstVaH264Dec GstVaH264Dec;
typedef struct _GstVaH264DecClass GstVaH264DecClass;

struct _GstVaH264DecClass
{
  GstH264DecoderClass parent_class;

  gchar *render_device_path;
};

struct _GstVaH264Dec
{
  GstH264Decoder parent;

  GstVaDisplay *display;
  GstVaDecoder *decoder;

  GstBufferPool *other_pool;

  GstFlowReturn last_ret;
  GstVideoCodecState *output_state;

  VAProfile profile;
  gint display_width;
  gint display_height;
  gint coded_width;
  gint coded_height;
  guint rt_format;
  gint dpb_size;

  gboolean need_negotiation;
  gboolean need_cropping;
  gboolean has_videometa;
  gboolean copy_frames;
};

static GstElementClass *parent_class = NULL;

struct CData
{
  gchar *render_device_path;
  gchar *description;
  GstCaps *sink_caps;
  GstCaps *src_caps;
};

/* *INDENT-OFF* */
static const gchar *src_caps_str = GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:VAMemory",
            "{ NV12, P010_10LE }") " ;" GST_VIDEO_CAPS_MAKE ("{ NV12, P010_10LE }");
/* *INDENT-ON* */

static const gchar *sink_caps_str = "video/x-h264";

static gboolean
gst_va_h264_dec_end_picture (GstH264Decoder * decoder, GstH264Picture * picture)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  GstVaDecodePicture *va_pic;

  GST_LOG_OBJECT (self, "end picture %p, (poc %d)",
      picture, picture->pic_order_cnt);

  va_pic = gst_h264_picture_get_user_data (picture);

  return gst_va_decoder_decode (self->decoder, va_pic);
}

static gboolean
_copy_output_buffer (GstVaH264Dec * self, GstVideoCodecFrame * codec_frame)
{
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  GstVideoInfo dest_vinfo;
  GstBuffer *buffer;
  GstFlowReturn ret;

  if (!self->other_pool)
    return FALSE;

  if (!gst_buffer_pool_set_active (self->other_pool, TRUE))
    return FALSE;

  gst_video_info_set_format (&dest_vinfo,
      GST_VIDEO_INFO_FORMAT (&self->output_state->info), self->display_width,
      self->display_height);

  ret = gst_buffer_pool_acquire_buffer (self->other_pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto fail;

  if (!gst_video_frame_map (&src_frame, &self->output_state->info,
          codec_frame->output_buffer, GST_MAP_READ))
    goto fail;

  if (!gst_video_frame_map (&dest_frame, &dest_vinfo, buffer, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  /* gst_video_frame_copy can crop this, but does not know, so let
   * make it think it's all right */
  GST_VIDEO_INFO_WIDTH (&src_frame.info) = self->display_width;
  GST_VIDEO_INFO_HEIGHT (&src_frame.info) = self->display_height;

  if (!gst_video_frame_copy (&dest_frame, &src_frame)) {
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dest_frame);
  gst_buffer_replace (&codec_frame->output_buffer, buffer);
  gst_buffer_unref (buffer);

  return TRUE;

fail:
  GST_ERROR_OBJECT (self, "Failed copy output buffer.");
  return FALSE;
}


static GstFlowReturn
gst_va_h264_dec_output_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->pic_order_cnt);

  if (self->last_ret != GST_FLOW_OK) {
    gst_h264_picture_unref (picture);
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    return self->last_ret;
  }

  if (self->copy_frames)
    _copy_output_buffer (self, frame);

  GST_BUFFER_PTS (frame->output_buffer) = GST_BUFFER_PTS (frame->input_buffer);
  GST_BUFFER_DTS (frame->output_buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (frame->output_buffer) =
      GST_BUFFER_DURATION (frame->input_buffer);

  GST_LOG_OBJECT (self, "Finish frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (frame->output_buffer)));

  gst_h264_picture_unref (picture);

  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
}

static void
_init_vaapi_pic (VAPictureH264 * va_picture)
{
  va_picture->picture_id = VA_INVALID_ID;
  va_picture->frame_idx = 0;
  va_picture->flags = VA_PICTURE_H264_INVALID;
  va_picture->TopFieldOrderCnt = 0;
  va_picture->BottomFieldOrderCnt = 0;
}

static void
_fill_vaapi_pic (VAPictureH264 * va_picture, GstH264Picture * picture)
{
  GstVaDecodePicture *va_pic;

  va_pic = gst_h264_picture_get_user_data (picture);

  if (!va_pic) {
    _init_vaapi_pic (va_picture);
    return;
  }

  va_picture->picture_id = va_pic->surface;
  va_picture->flags = 0;

  if (picture->ref && picture->long_term) {
    va_picture->flags |= VA_PICTURE_H264_LONG_TERM_REFERENCE;
    va_picture->frame_idx = picture->long_term_frame_idx;
  } else {
    if (picture->ref)
      va_picture->flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    va_picture->frame_idx = picture->frame_num;
  }

  switch (picture->field) {
    case GST_H264_PICTURE_FIELD_FRAME:
      va_picture->TopFieldOrderCnt = picture->top_field_order_cnt;
      va_picture->BottomFieldOrderCnt = picture->bottom_field_order_cnt;
      break;
    case GST_H264_PICTURE_FIELD_TOP_FIELD:
      va_picture->flags |= VA_PICTURE_H264_TOP_FIELD;
      va_picture->TopFieldOrderCnt = picture->top_field_order_cnt;
      va_picture->BottomFieldOrderCnt = 0;
      break;
    case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
      va_picture->flags |= VA_PICTURE_H264_BOTTOM_FIELD;
      va_picture->TopFieldOrderCnt = 0;
      va_picture->BottomFieldOrderCnt = picture->bottom_field_order_cnt;
      break;
    default:
      va_picture->TopFieldOrderCnt = 0;
      va_picture->BottomFieldOrderCnt = 0;
      break;
  }
}

/* fill the VA API reference picture lists from the GstCodec reference
 * picture list */
static void
_fill_ref_pic_list (VAPictureH264 va_reflist[32], GArray * reflist)
{
  guint i;

  for (i = 0; i < reflist->len; i++) {
    GstH264Picture *picture = g_array_index (reflist, GstH264Picture *, i);
    _fill_vaapi_pic (&va_reflist[i], picture);
  }

  for (; i < 32; i++)
    _init_vaapi_pic (&va_reflist[i]);
}

static void
_fill_pred_weight_table (GstH264SliceHdr * header,
    VASliceParameterBufferH264 * slice_param)
{
  GstH264PPS *pps;
  GstH264SPS *sps;
  guint num_weight_tables = 0;
  gint i, j;

  pps = header->pps;
  sps = pps->sequence;

  if (pps->weighted_pred_flag
      && (GST_H264_IS_P_SLICE (header) || GST_H264_IS_SP_SLICE (header)))
    num_weight_tables = 1;
  else if (pps->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE (header))
    num_weight_tables = 2;

  if (num_weight_tables == 0)
    return;

  slice_param->luma_log2_weight_denom =
      header->pred_weight_table.luma_log2_weight_denom;
  slice_param->chroma_log2_weight_denom =
      header->pred_weight_table.chroma_log2_weight_denom;

  /* VA API also wants the inferred (default) values, not only what is
   * available in the bitstream (7.4.3.2). */

  slice_param->luma_weight_l0_flag = 1;
  for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
    slice_param->luma_weight_l0[i] =
        header->pred_weight_table.luma_weight_l0[i];
    slice_param->luma_offset_l0[i] =
        header->pred_weight_table.luma_offset_l0[i];
  }

  slice_param->chroma_weight_l0_flag = sps->chroma_array_type != 0;
  if (slice_param->chroma_weight_l0_flag) {
    for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
      for (j = 0; j < 2; j++) {
        slice_param->chroma_weight_l0[i][j] =
            header->pred_weight_table.chroma_weight_l0[i][j];
        slice_param->chroma_offset_l0[i][j] =
            header->pred_weight_table.chroma_offset_l0[i][j];
      }
    }
  }

  if (num_weight_tables == 1)
    return;

  slice_param->luma_weight_l1_flag = 1;
  for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
    slice_param->luma_weight_l1[i] =
        header->pred_weight_table.luma_weight_l1[i];
    slice_param->luma_offset_l1[i] =
        header->pred_weight_table.luma_offset_l1[i];
  }

  slice_param->chroma_weight_l1_flag = sps->chroma_array_type != 0;
  if (slice_param->chroma_weight_l1_flag) {
    for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
      for (j = 0; j < 2; j++) {
        slice_param->chroma_weight_l1[i][j] =
            header->pred_weight_table.chroma_weight_l1[i][j];
        slice_param->chroma_offset_l1[i][j] =
            header->pred_weight_table.chroma_offset_l1[i][j];
      }
    }
  }
}

static inline guint
_get_slice_data_bit_offset (GstH264SliceHdr * header, guint nal_header_bytes)
{
  guint epb_count;

  epb_count = header->n_emulation_prevention_bytes;
  return 8 * nal_header_bytes + header->header_size - epb_count * 8;
}

static gboolean
gst_va_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1)
{
  GstH264SliceHdr *header = &slice->header;
  GstH264NalUnit *nalu = &slice->nalu;
  GstVaDecodePicture *va_pic;
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  VASliceParameterBufferH264 slice_param;
  gboolean ret;

  GST_TRACE_OBJECT (self, "-");

  /* *INDENT-OFF* */
  slice_param = (VASliceParameterBufferH264) {
    .slice_data_size = nalu->size,
    .slice_data_offset = 0,
    .slice_data_flag = VA_SLICE_DATA_FLAG_ALL,
    .slice_data_bit_offset =
        _get_slice_data_bit_offset (header, nalu->header_bytes),
    .first_mb_in_slice = header->first_mb_in_slice,
    .slice_type = header->type % 5,
    .direct_spatial_mv_pred_flag = header->direct_spatial_mv_pred_flag,
    .cabac_init_idc = header->cabac_init_idc,
    .slice_qp_delta = header->slice_qp_delta,
    .disable_deblocking_filter_idc = header->disable_deblocking_filter_idc,
    .slice_alpha_c0_offset_div2 = header->slice_alpha_c0_offset_div2,
    .slice_beta_offset_div2 = header->slice_beta_offset_div2,
    .num_ref_idx_l0_active_minus1 = header->num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_active_minus1 = header->num_ref_idx_l1_active_minus1,
  };
  /* *INDENT-ON* */

  _fill_ref_pic_list (slice_param.RefPicList0, ref_pic_list0);
  _fill_ref_pic_list (slice_param.RefPicList1, ref_pic_list1);

  _fill_pred_weight_table (header, &slice_param);

  va_pic = gst_h264_picture_get_user_data (picture);

  ret = gst_va_decoder_add_slice_buffer (self->decoder, va_pic, &slice_param,
      sizeof (slice_param), slice->nalu.data + slice->nalu.offset,
      slice->nalu.size);
  if (!ret) {
    gst_va_decoder_destroy_buffers (self->decoder, va_pic);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_va_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstH264PPS *pps;
  GstH264SPS *sps;
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  GstVaDecodePicture *va_pic;
  VAIQMatrixBufferH264 iq_matrix = { 0, };
  VAPictureParameterBufferH264 pic_param;
  guint i, n;

  GST_TRACE_OBJECT (self, "-");

  va_pic = gst_h264_picture_get_user_data (picture);

  pps = slice->header.pps;
  sps = pps->sequence;

  /* *INDENT-OFF* */
  pic_param = (VAPictureParameterBufferH264) {
    /* .CurrPic */
    /* .ReferenceFrames */
    .picture_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1,
    .picture_height_in_mbs_minus1 =
        sps->pic_height_in_map_units_minus1 << !sps->frame_mbs_only_flag,
    .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
    .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
    .num_ref_frames = sps->num_ref_frames,
    .seq_fields.bits = {
      .chroma_format_idc = sps->chroma_format_idc,
      .residual_colour_transform_flag = sps->separate_colour_plane_flag,
      .gaps_in_frame_num_value_allowed_flag =
          sps->gaps_in_frame_num_value_allowed_flag,
      .frame_mbs_only_flag = sps->frame_mbs_only_flag,
      .mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag,
      .direct_8x8_inference_flag = sps->direct_8x8_inference_flag,
      .MinLumaBiPredSize8x8 = sps->level_idc >= 31, /* A.3.3.2 */
      .log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4,
      .pic_order_cnt_type = sps->pic_order_cnt_type,
      .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
      .delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag,
    },
    .pic_init_qp_minus26 = pps->pic_init_qp_minus26,
    .pic_init_qs_minus26 = pps->pic_init_qs_minus26,
    .chroma_qp_index_offset = pps->chroma_qp_index_offset,
    .second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset,
    .pic_fields.bits = {
      .entropy_coding_mode_flag = pps->entropy_coding_mode_flag,
      .weighted_pred_flag = pps->weighted_pred_flag,
      .weighted_bipred_idc = pps->weighted_bipred_idc,
      .transform_8x8_mode_flag = pps->transform_8x8_mode_flag,
      .field_pic_flag = slice->header.field_pic_flag,
      .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
      .pic_order_present_flag = pps->pic_order_present_flag,
      .deblocking_filter_control_present_flag =
          pps->deblocking_filter_control_present_flag,
      .redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag,
      .reference_pic_flag = picture->nal_ref_idc != 0,
    },
    .frame_num = slice->header.frame_num,
  };
  /* *INDENT-ON* */

  _fill_vaapi_pic (&pic_param.CurrPic, picture);

  /* reference frames */
  {
    GArray *ref_list = g_array_sized_new (FALSE, FALSE,
        sizeof (GstH264Picture *), 16);
    g_array_set_clear_func (ref_list, (GDestroyNotify) gst_h264_picture_clear);

    gst_h264_dpb_get_pictures_short_term_ref (dpb, ref_list);
    for (i = 0; i < 16 && i < ref_list->len; i++) {
      GstH264Picture *pic = g_array_index (ref_list, GstH264Picture *, i);
      _fill_vaapi_pic (&pic_param.ReferenceFrames[i], pic);
    }
    g_array_set_size (ref_list, 0);

    gst_h264_dpb_get_pictures_long_term_ref (dpb, ref_list);
    for (; i < 16 && i < ref_list->len; i++) {
      GstH264Picture *pic = g_array_index (ref_list, GstH264Picture *, i);
      _fill_vaapi_pic (&pic_param.ReferenceFrames[i], pic);
    }
    g_array_unref (ref_list);

    for (; i < 16; i++)
      _init_vaapi_pic (&pic_param.ReferenceFrames[i]);
  }

  if (!gst_va_decoder_add_param_buffer (self->decoder, va_pic,
          VAPictureParameterBufferType, &pic_param, sizeof (pic_param)))
    goto fail;

  /* there are always 6 4x4 scaling lists */
  for (i = 0; i < 6; i++) {
    gst_h264_quant_matrix_4x4_get_raster_from_zigzag (iq_matrix.ScalingList4x4
        [i], pps->scaling_lists_4x4[i]);
  }

  /* We need the first 2 entries (Y intra and Y inter for YCbCr 4:2:2 and
   * less, and the full 6 entries for 4:4:4, see Table 7-2 of the spec for
   * more details */
  n = (pps->sequence->chroma_format_idc == 3) ? 6 : 2;
  for (i = 0; i < n; i++) {
    gst_h264_quant_matrix_8x8_get_raster_from_zigzag (iq_matrix.ScalingList8x8
        [i], pps->scaling_lists_8x8[i]);
  }

  if (!gst_va_decoder_add_param_buffer (self->decoder, va_pic,
          VAIQMatrixBufferType, &iq_matrix, sizeof (iq_matrix)))
    goto fail;

  return TRUE;

fail:
  {
    gst_va_decoder_destroy_buffers (self->decoder, va_pic);
    return FALSE;
  }
}

static gboolean
gst_va_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  GstVaDecodePicture *pic;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  VASurfaceID surface;

  self->last_ret = gst_video_decoder_allocate_output_frame (vdec, frame);
  if (self->last_ret != GST_FLOW_OK)
    goto error;

  surface = gst_va_buffer_get_surface (frame->output_buffer, NULL);

  pic = gst_va_decode_picture_new (surface);
  gst_h264_picture_set_user_data (picture, pic,
      (GDestroyNotify) gst_va_decode_picture_free);

  GST_LOG_OBJECT (self, "New va decode picture %p - %#x", pic, pic->surface);

  return TRUE;

error:
  {
    GST_WARNING_OBJECT (self,
        "Failed to allocated output buffer, return %s",
        gst_flow_get_name (self->last_ret));
    return FALSE;
  }
}

static inline guint
_get_num_views (const GstH264SPS * sps)
{
  return 1 + (sps->extension_type == GST_H264_NAL_EXTENSION_MVC ?
      sps->extension.mvc.num_views_minus1 : 0);
}

static guint
_get_rtformat (GstVaH264Dec * self, guint8 bit_depth_luma,
    guint8 chroma_format_idc)
{
  switch (bit_depth_luma) {
    case 10:
      if (chroma_format_idc == 3)
        return VA_RT_FORMAT_YUV444_10;
      if (chroma_format_idc == 2)
        return VA_RT_FORMAT_YUV422_10;
      else
        return VA_RT_FORMAT_YUV420_10;
      break;
    case 8:
      if (chroma_format_idc == 3)
        return VA_RT_FORMAT_YUV444;
      if (chroma_format_idc == 2)
        return VA_RT_FORMAT_YUV422;
      else
        return VA_RT_FORMAT_YUV420;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported chroma format: %d "
          "(with depth luma: %d)", chroma_format_idc, bit_depth_luma);
      return 0;
  }
}

/* *INDENT-OFF* */
static const struct
{
  GstH264Profile profile_idc;
  VAProfile va_profile;
} profile_map[] = {
#define P(idc, va) { G_PASTE (GST_H264_PROFILE_, idc), G_PASTE (VAProfileH264, va) }
  /* P (BASELINE, ), */
  P (MAIN, Main),
  /* P (EXTENDED, ), */
  P (HIGH, High),
  /* P (HIGH10, ), */
  /* P (HIGH_422, ), */
  /* P (HIGH_444, ), */
  P (MULTIVIEW_HIGH, MultiviewHigh),
  P (STEREO_HIGH, StereoHigh),
  /* P (SCALABLE_BASELINE, ), */
  /* P (SCALABLE_HIGH, ), */
#undef P
};
/* *INDENT-ON* */

static VAProfile
_get_profile (GstVaH264Dec * self, const GstH264SPS * sps, gint max_dpb_size)
{
  VAProfile profiles[4];
  gint i = 0, j;

  for (j = 0; j < G_N_ELEMENTS (profile_map); j++) {
    if (profile_map[j].profile_idc == sps->profile_idc) {
      profiles[i++] = profile_map[j].va_profile;
      break;
    }
  }

  switch (sps->profile_idc) {
    case GST_H264_PROFILE_BASELINE:
      if (sps->constraint_set1_flag) {  /* A.2.2 (main profile) */
        profiles[i++] = VAProfileH264ConstrainedBaseline;
        profiles[i++] = VAProfileH264Main;
      }
      break;
    case GST_H264_PROFILE_EXTENDED:
      if (sps->constraint_set1_flag) {  /* A.2.2 (main profile) */
        profiles[i++] = VAProfileH264Main;
      }
      break;
    case GST_H264_PROFILE_MULTIVIEW_HIGH:
      if (_get_num_views (sps) == 2) {
        profiles[i++] = VAProfileH264StereoHigh;
      }
      if (max_dpb_size <= 16 /* && i965 driver */ ) {
        profiles[i++] = VAProfileH264MultiviewHigh;
      }
    default:
      break;
  }

  for (j = 0; j < i && j < G_N_ELEMENTS (profiles); j++) {
    if (gst_va_decoder_has_profile (self->decoder, profiles[j]))
      return profiles[j];
  }

  GST_ERROR_OBJECT (self, "Unsupported profile: %d", sps->profile_idc);

  return VAProfileNone;
}

static gboolean
_format_changed (GstVaH264Dec * self, VAProfile new_profile, guint new_rtformat,
    gint new_width, gint new_height)
{
  VAProfile profile = VAProfileNone;
  guint rt_format = VA_RT_FORMAT_YUV420;
  gint width = 0, height = 0;

  g_object_get (self->decoder, "va-profile", &profile, "va-rt-format",
      &rt_format, "coded-width", &width, "coded-height", &height, NULL);

  /* @TODO: Check if current buffers are large enough, and reuse
   * them */
  return !(profile == new_profile && rt_format == new_rtformat
      && width == new_width && height == new_height);
}

static void
_set_latency (GstVaH264Dec * self, const GstH264SPS * sps)
{
  GstClockTime duration, min, max;
  gint fps_d, fps_n;
  guint32 num_reorder_frames;

  fps_d = self->output_state->info.fps_d;
  fps_n = self->output_state->info.fps_n;

  /* if 0/1 then 25/1 */
  if (fps_n == 0) {
    fps_n = 25;
    fps_d = 1;
  }

  num_reorder_frames = 1;
  if (sps->vui_parameters_present_flag
      && sps->vui_parameters.bitstream_restriction_flag)
    num_reorder_frames = sps->vui_parameters.num_reorder_frames;
  if (num_reorder_frames > self->dpb_size)
    num_reorder_frames = 1;

  duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  min = num_reorder_frames * duration;
  max = self->dpb_size * duration;

  GST_LOG_OBJECT (self,
      "latency min %" G_GUINT64_FORMAT " max %" G_GUINT64_FORMAT, min, max);

  gst_video_decoder_set_latency (GST_VIDEO_DECODER (self), min, max);
}

static gboolean
gst_va_h264_dec_new_sequence (GstH264Decoder * decoder, const GstH264SPS * sps,
    gint max_dpb_size)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  VAProfile profile;
  gint display_width;
  gint display_height;
  guint rt_format;
  gboolean negotiation_needed = FALSE;

  if (self->dpb_size < max_dpb_size)
    self->dpb_size = max_dpb_size;

  if (sps->frame_cropping_flag) {
    display_width = sps->crop_rect_width;
    display_height = sps->crop_rect_height;
  } else {
    display_width = sps->width;
    display_height = sps->height;
  }

  profile = _get_profile (self, sps, max_dpb_size);
  if (profile == VAProfileNone)
    return FALSE;

  rt_format = _get_rtformat (self, sps->bit_depth_luma_minus8 + 8,
      sps->chroma_format_idc);
  if (rt_format == 0)
    return FALSE;

  if (_format_changed (self, profile, rt_format, sps->width, sps->height)) {
    self->profile = profile;
    self->rt_format = rt_format;
    self->coded_width = sps->width;
    self->coded_height = sps->height;

    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Format changed to %s [%x] (%dx%d)",
        gst_va_profile_name (profile), rt_format, self->coded_width,
        self->coded_height);
  }

  if (self->display_width != display_width
      || self->display_height != display_height) {
    self->display_width = display_width;
    self->display_height = display_height;

    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Resolution changed to %dx%d", self->display_width,
        self->display_height);
  }

  self->need_cropping = self->display_width < self->coded_width
      || self->display_height < self->coded_height;

  if (negotiation_needed) {
    self->need_negotiation = TRUE;
    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }

    _set_latency (self, sps);
  }

  return TRUE;
}

static gboolean
gst_va_h264_dec_open (GstVideoDecoder * decoder)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  GstVaH264DecClass *klass = GST_VA_H264_DEC_GET_CLASS (decoder);

  if (!gst_va_ensure_element_data (decoder, klass->render_device_path,
          &self->display))
    return FALSE;

  if (!self->decoder)
    self->decoder = gst_va_decoder_new (self->display, H264);

  return (self->decoder != NULL);
}

static gboolean
gst_va_h264_dec_close (GstVideoDecoder * decoder)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);

  gst_clear_object (&self->decoder);
  gst_clear_object (&self->display);

  return TRUE;
}

static GstCaps *
_complete_sink_caps (GstCaps * sinkcaps)
{
  GstCaps *caps = gst_caps_copy (sinkcaps);
  GValue val = G_VALUE_INIT;
  const gchar *streamformat[] = { "avc", "avc3", "byte-stream" };
  gint i;

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, "au");
  gst_caps_set_value (caps, "alignment", &val);
  g_value_unset (&val);

  gst_value_list_init (&val, G_N_ELEMENTS (streamformat));
  for (i = 0; i < G_N_ELEMENTS (streamformat); i++) {
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, streamformat[i]);
    gst_value_list_append_value (&val, &v);
    g_value_unset (&v);
  }
  gst_caps_set_value (caps, "stream-format", &val);
  g_value_unset (&val);

  return caps;
}

static GstCaps *
gst_va_h264_dec_sink_getcaps (GstVideoDecoder * decoder, GstCaps * filter)
{
  GstCaps *sinkcaps, *caps = NULL, *tmp;
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);

  if (self->decoder)
    caps = gst_va_decoder_get_sinkpad_caps (self->decoder);

  if (caps) {
    sinkcaps = _complete_sink_caps (caps);
    gst_caps_unref (caps);
    if (filter) {
      tmp = gst_caps_intersect_full (filter, sinkcaps,
          GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (sinkcaps);
      caps = tmp;
    } else {
      caps = sinkcaps;
    }
    GST_LOG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);
  } else if (!caps) {
    caps = gst_video_decoder_proxy_getcaps (decoder, NULL, filter);
  }

  return caps;
}

static gboolean
gst_va_h264_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      return gst_va_handle_context_query (GST_ELEMENT_CAST (self), query,
          self->display);
    }
    case GST_QUERY_CAPS:{
      GstCaps *caps = NULL, *tmp, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      if (self->decoder)
        caps = gst_va_decoder_get_srcpad_caps (self->decoder);
      if (caps) {
        if (filter) {
          tmp =
              gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
          gst_caps_unref (caps);
          caps = tmp;
        }

        GST_LOG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        ret = TRUE;
        break;
      }
      /* else jump to default */
    }
    default:
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_va_h264_dec_sink_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    return gst_va_handle_context_query (GST_ELEMENT_CAST (self), query,
        self->display);
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_query (decoder, query);
}

static gboolean
gst_va_h264_dec_stop (GstVideoDecoder * decoder)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);

  if (!gst_va_decoder_close (self->decoder))
    return FALSE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  if (self->other_pool)
    gst_buffer_pool_set_active (self->other_pool, FALSE);
  gst_clear_object (&self->other_pool);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static GstVideoFormat
_default_video_format_from_chroma (guint chroma_type)
{
  switch (chroma_type) {
    case VA_RT_FORMAT_YUV420:
    case VA_RT_FORMAT_YUV422:
    case VA_RT_FORMAT_YUV444:
      return GST_VIDEO_FORMAT_NV12;
    case VA_RT_FORMAT_YUV420_10:
    case VA_RT_FORMAT_YUV422_10:
    case VA_RT_FORMAT_YUV444_10:
      return GST_VIDEO_FORMAT_P010_10LE;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

static void
_get_preferred_format_and_caps_features (GstVaH264Dec * self,
    GstVideoFormat * format, GstCapsFeatures ** capsfeatures)
{
  GstCaps *peer_caps, *preferred_caps = NULL;
  GstCapsFeatures *features;
  GstStructure *structure;
  const GValue *v_format;
  guint num_structures, i;

  peer_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_DEBUG_OBJECT (self, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  /* prefer memory:VASurface over other caps features */
  num_structures = gst_caps_get_size (peer_caps);
  for (i = 0; i < num_structures; i++) {
    features = gst_caps_get_features (peer_caps, i);
    structure = gst_caps_get_structure (peer_caps, i);

    if (gst_caps_features_is_any (features))
      continue;

    if (gst_caps_features_contains (features, "memory:VAMemory")) {
      preferred_caps = gst_caps_new_full (gst_structure_copy (structure), NULL);
      gst_caps_set_features_simple (preferred_caps,
          gst_caps_features_copy (features));
      break;
    }
  }

  if (!preferred_caps)
    preferred_caps = peer_caps;
  else
    gst_clear_caps (&peer_caps);

  if (gst_caps_is_empty (preferred_caps)
      || gst_caps_is_any (preferred_caps)) {
    /* if any or not linked yet then system memory and nv12 */
    if (capsfeatures)
      *capsfeatures = NULL;
    if (format)
      *format = _default_video_format_from_chroma (self->rt_format);
    goto bail;
  }

  features = gst_caps_get_features (preferred_caps, 0);
  if (features && capsfeatures)
    *capsfeatures = gst_caps_features_copy (features);

  if (!format)
    goto bail;

  structure = gst_caps_get_structure (preferred_caps, 0);
  v_format = gst_structure_get_value (structure, "format");
  if (!v_format)
    *format = _default_video_format_from_chroma (self->rt_format);
  else if (G_VALUE_HOLDS_STRING (v_format))
    *format = gst_video_format_from_string (g_value_get_string (v_format));
  else if (GST_VALUE_HOLDS_LIST (v_format)) {
    guint num_values = gst_value_list_get_size (v_format);
    for (i = 0; i < num_values; i++) {
      GstVideoFormat fmt;
      const GValue *v_fmt = gst_value_list_get_value (v_format, i);
      if (!v_fmt)
        continue;
      fmt = gst_video_format_from_string (g_value_get_string (v_fmt));
      if (gst_va_chroma_from_video_format (fmt) == self->rt_format) {
        *format = fmt;
        break;
      }
    }
    if (i == num_values)
      *format = _default_video_format_from_chroma (self->rt_format);
  }

bail:
  gst_clear_caps (&preferred_caps);
}

static gboolean
gst_va_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  GstCapsFeatures *capsfeatures = NULL;
  GstH264Decoder *h264dec = GST_H264_DECODER (decoder);

  /* Ignore downstream renegotiation request. */
  if (!self->need_negotiation)
    return TRUE;

  self->need_negotiation = FALSE;

  if (gst_va_decoder_is_open (self->decoder)
      && !gst_va_decoder_close (self->decoder))
    return FALSE;

  if (!gst_va_decoder_open (self->decoder, self->profile, self->rt_format))
    return FALSE;

  if (!gst_va_decoder_set_format (self->decoder, self->coded_width,
          self->coded_height, NULL))
    return FALSE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  _get_preferred_format_and_caps_features (self, &format, &capsfeatures);

  self->output_state =
      gst_video_decoder_set_output_state (decoder, format,
      self->display_width, self->display_height, h264dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);
  if (capsfeatures)
    gst_caps_set_features_simple (self->output_state->caps, capsfeatures);

  GST_INFO_OBJECT (self, "Negotiated caps %" GST_PTR_FORMAT,
      self->output_state->caps);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static inline gboolean
_caps_is_dmabuf (GstVaH264Dec * self, GstCaps * caps)
{
  GstCapsFeatures *features;

  features = gst_caps_get_features (caps, 0);
  return gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)
      && (gst_va_decoder_get_mem_types (self->decoder)
      & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME);
}

static inline gboolean
_caps_is_va_memory (GstCaps * caps)
{
  GstCapsFeatures *features;

  features = gst_caps_get_features (caps, 0);
  return gst_caps_features_contains (features, "memory:VAMemory");
}

static inline void
_shall_copy_frames (GstVaH264Dec * self, GstVideoInfo * info)
{
  GstVideoInfo ref_info;
  guint i;

  self->copy_frames = FALSE;

  if (self->has_videometa)
    return;

  gst_video_info_set_format (&ref_info, GST_VIDEO_INFO_FORMAT (info),
      self->display_width, self->display_height);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    if (info->stride[i] != ref_info.stride[i] ||
        info->offset[i] != ref_info.offset[i]) {
      GST_WARNING_OBJECT (self,
          "GstVideoMeta support required, copying frames.");
      self->copy_frames = TRUE;
      break;
    }
  }
}

static gboolean
_try_allocator (GstVaH264Dec * self, GstAllocator * allocator, GstCaps * caps,
    guint * size)
{
  GstVaAllocationParams params = {
    .usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_DECODER,
  };

  if (!gst_video_info_from_caps (&params.info, caps))
    return FALSE;
  if (self->need_cropping) {
    GST_VIDEO_INFO_WIDTH (&params.info) = self->coded_width;
    GST_VIDEO_INFO_HEIGHT (&params.info) = self->coded_height;
  }

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    if (!gst_va_dmabuf_try (allocator, &params))
      return FALSE;
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    if (!gst_va_allocator_try (allocator, &params))
      return FALSE;
    if (!_caps_is_va_memory (caps))
      _shall_copy_frames (self, &params.info);
  } else {
    return FALSE;
  }

  if (size)
    *size = GST_VIDEO_INFO_SIZE (&params.info);

  return TRUE;
}

static GstAllocator *
_create_allocator (GstVaH264Dec * self, GstCaps * caps, guint * size)
{
  GstAllocator *allocator = NULL;
  GstVaDisplay *display = NULL;

  g_object_get (self->decoder, "display", &display, NULL);

  if (_caps_is_dmabuf (self, caps))
    allocator = gst_va_dmabuf_allocator_new (display);
  else {
    GArray *surface_formats =
        gst_va_decoder_get_surface_formats (self->decoder);
    allocator = gst_va_allocator_new (display, surface_formats);
  }

  gst_object_unref (display);

  if (!_try_allocator (self, allocator, caps, size))
    gst_clear_object (&allocator);

  return allocator;
}

/* 1. get allocator in query
 *    1.1 if allocator is not ours and downstream doesn't handle
 *        videometa, keep it for other_pool
 * 2. get pool in query
 *    2.1 if pool is not va, keep it as other_pool if downstream
 *        doesn't handle videometa or (it doesn't handle alignment and
 *        the stream needs cropping)
 *    2.2 if there's no pool in query and downstream doesn't handle
 *        videometa, create other_pool as GstVideoPool with the non-va
 *        from query and query's params
 * 3. create our allocator and pool if they aren't in query
 * 4. add or update pool and allocator in query
 * 5. set our custom pool configuration
 */
static gboolean
gst_va_h264_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstAllocator *allocator = NULL, *other_allocator = NULL;
  GstAllocationParams other_params, params;
  GstBufferPool *pool = NULL;
  GstCaps *caps = NULL;
  GstStructure *config;
  GstVideoInfo info;
  GstVaH264Dec *self = GST_VA_H264_DEC (decoder);
  guint size, min, max;
  gboolean update_pool = FALSE, update_allocator = FALSE, has_videoalignment;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!(caps && gst_video_info_from_caps (&info, caps)))
    goto wrong_caps;

  self->has_videometa = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &other_params);
    if (allocator && !(GST_IS_VA_DMABUF_ALLOCATOR (allocator)
            || GST_IS_VA_ALLOCATOR (allocator))) {
      /* save the allocator for the other pool */
      other_allocator = allocator;
      allocator = NULL;
    }
    update_allocator = TRUE;
  } else {
    gst_allocation_params_init (&other_params);
  }

  gst_allocation_params_init (&params);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_VA_POOL (pool)) {
        has_videoalignment = gst_buffer_pool_has_option (pool,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
        if (!self->has_videometa || (!has_videoalignment
                && self->need_cropping)) {
          GST_DEBUG_OBJECT (self,
              "keeping other pool for copy %" GST_PTR_FORMAT, pool);
          gst_object_replace ((GstObject **) & self->other_pool,
              (GstObject *) pool);
          gst_object_unref (pool);      /* decrease previous increase */
        }
        gst_clear_object (&pool);
      }
    }

    min = MAX (16 + 4, min);    /* max num pic references + scratch surfaces */
    size = MAX (size, GST_VIDEO_INFO_SIZE (&info));

    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&info);

    if (!self->has_videometa && !_caps_is_va_memory (caps)) {
      GST_DEBUG_OBJECT (self, "making new other pool for copy");
      self->other_pool = gst_video_buffer_pool_new ();
      config = gst_buffer_pool_get_config (self->other_pool);
      gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
      gst_buffer_pool_config_set_allocator (config, other_allocator,
          &other_params);
      if (!gst_buffer_pool_set_config (self->other_pool, config)) {
        GST_ERROR_OBJECT (self, "couldn't configure other pool for copy");
        gst_clear_object (&self->other_pool);
      }
    } else {
      gst_clear_object (&other_allocator);
    }

    min = 16 + 4;               /* max num pic references + scratch surfaces */
    max = 0;
  }

  if (!allocator) {
    if (!(allocator = _create_allocator (self, caps, &size)))
      return FALSE;
  }

  if (!pool)
    pool = gst_va_pool_new ();

  {
    GstStructure *config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (self->need_cropping) {
      GstVideoAlignment video_align = {
        .padding_bottom = self->coded_height - self->display_height,
        .padding_left = self->coded_width - self->display_width,
      };
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      gst_buffer_pool_config_set_video_alignment (config, &video_align);
    }

    gst_buffer_pool_config_set_va_allocation_params (config,
        VA_SURFACE_ATTRIB_USAGE_HINT_DECODER);

    if (!gst_buffer_pool_set_config (pool, config))
      return FALSE;
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (allocator);
  gst_object_unref (pool);

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
      query);

wrong_caps:
  {
    GST_WARNING_OBJECT (self, "No valid caps");
    return FALSE;
  }
}

static void
gst_va_h264_dec_set_context (GstElement * element, GstContext * context)
{
  GstVaDisplay *old_display, *new_display;
  GstVaH264Dec *self = GST_VA_H264_DEC (element);
  GstVaH264DecClass *klass = GST_VA_H264_DEC_GET_CLASS (self);
  gboolean ret;

  old_display = self->display ? gst_object_ref (self->display) : NULL;
  ret = gst_va_handle_set_context (element, context, klass->render_device_path,
      &self->display);
  new_display = self->display ? gst_object_ref (self->display) : NULL;

  if (!ret
      || (old_display && new_display && old_display != new_display
          && self->decoder)) {
    GST_ELEMENT_WARNING (element, RESOURCE, BUSY,
        ("Can't replace VA display while operating"), (NULL));
  }

  gst_clear_object (&old_display);
  gst_clear_object (&new_display);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static void
gst_va_h264_dec_dispose (GObject * object)
{
  gst_va_h264_dec_close (GST_VIDEO_DECODER (object));
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_h264_dec_class_init (gpointer g_class, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (g_class);
  GstVaH264DecClass *klass = GST_VA_H264_DEC_CLASS (g_class);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (g_class);
  struct CData *cdata = class_data;
  gchar *long_name;

  parent_class = g_type_class_peek_parent (g_class);

  klass->render_device_path = g_strdup (cdata->render_device_path);

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API H.264 Decoder in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API H.264 Decoder");
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "VA-API based H.264 video decoder",
      "Víctor Jáquez <vjaquez@igalia.com>");

  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);
  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  gst_pad_template_set_documentation_caps (sink_pad_templ, sink_doc_caps);
  gst_caps_unref (sink_doc_caps);

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      cdata->src_caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);
  src_doc_caps = gst_caps_from_string (src_caps_str);
  gst_pad_template_set_documentation_caps (src_pad_templ, src_doc_caps);
  gst_caps_unref (src_doc_caps);

  gobject_class->dispose = gst_va_h264_dec_dispose;

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_va_h264_dec_set_context);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_va_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_va_h264_dec_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_va_h264_dec_stop);
  decoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_va_h264_dec_sink_getcaps);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_va_h264_dec_src_query);
  decoder_class->sink_query = GST_DEBUG_FUNCPTR (gst_va_h264_dec_sink_query);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_va_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_decide_allocation);

  h264decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_new_sequence);
  h264decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_decode_slice);

  h264decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_new_picture);
  h264decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_output_picture);
  h264decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_start_picture);
  h264decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_va_h264_dec_end_picture);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);
}

static void
gst_va_h264_dec_init (GTypeInstance * instance, gpointer g_class)
{
  gst_h264_decoder_set_process_ref_pic_lists (GST_H264_DECODER (instance),
      TRUE);
}

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_h264dec_debug, "vah264dec", 0,
      "VA h264 decoder");

  return NULL;
}

gboolean
gst_va_h264_dec_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaH264DecClass),
    .class_init = gst_va_h264_dec_class_init,
    .instance_size = sizeof (GstVaH264Dec),
    .instance_init = gst_va_h264_dec_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (sink_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (src_caps), FALSE);

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);
  cdata->sink_caps = _complete_sink_caps (sink_caps);
  cdata->src_caps = gst_caps_ref (src_caps);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;

  type_name = g_strdup ("GstVaH264Dec");
  feature_name = g_strdup ("vah264dec");

  /* The first decoder to be registered should use a constant name,
   * like vah264dec, for any additional decoders, we create unique
   * names, using inserting the render device name. */
  if (g_type_from_name (type_name)) {
    gchar *basename = g_path_get_basename (device->render_device_path);
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstVa%sH264Dec", basename);
    feature_name = g_strdup_printf ("va%sh264dec", basename);
    cdata->description = basename;

    /* lower rank for non-first device */
    if (rank > 0)
      rank--;
  }

  g_once (&debug_once, _register_debug_category, NULL);

  type = g_type_register_static (GST_TYPE_H264_DECODER,
      type_name, &type_info, 0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
