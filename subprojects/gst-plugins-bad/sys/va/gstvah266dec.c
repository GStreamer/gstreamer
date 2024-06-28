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
 * SECTION:element-vah266dec
 * @title: vah266dec
 * @short_description: A VA-API based H266 video decoder
 *
 * vah266dec decodes H266 bitstreams to VA surfaces using the
 * installed and chosen [VA-API] driver.
 *
 * The decoding surfaces can be mapped onto main memory as video
 * frames.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=some.h266 ! h266parse ! vah266dec ! autovideosink
 * ```
 *
 * Since: 1.26
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvah266dec.h"

#include "gstvabasedec.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_h266dec_debug);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_va_h266dec_debug
#else
#define GST_CAT_DEFAULT NULL
#endif

#define GST_VA_H266_DEC(obj)           ((GstVaH266Dec *) obj)
#define GST_VA_H266_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaH266DecClass))
#define GST_VA_H266_DEC_CLASS(klass)   ((GstVaH266DecClass *) klass)

typedef struct _GstVaH266Dec GstVaH266Dec;
typedef struct _GstVaH266DecClass GstVaH266DecClass;

struct _GstVaH266DecClass
{
  GstVaBaseDecClass parent_class;
};

struct _GstVaH266Dec
{
  GstVaBaseDec parent;

  VAPictureParameterBufferVVC pic_param;

  gint dpb_size;
};

static GstElementClass *parent_class = NULL;

/* *INDENT-OFF* */
static const gchar *src_caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12, P010_10LE }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ NV12, P010_10LE }");
/* *INDENT-ON* */

static const gchar *sink_caps_str = "video/x-h266";

/* *INDENT-OFF* */
static const struct
{
  GstH266Profile profile_idc;
  VAProfile va_profile;
} profile_map[] = {
#define P(idc, va) { G_PASTE (GST_H266_PROFILE_, idc), G_PASTE (VAProfileVVC, va) }
  P (MAIN_10, Main10),
  P (MAIN_10_STILL_PICTURE, Main10),
  P (MULTILAYER_MAIN_10, MultilayerMain10),
  P (MULTILAYER_MAIN_10_STILL_PICTURE, MultilayerMain10),
#undef P
};
/* *INDENT-ON* */

static VAProfile
_get_profile (GstVaH266Dec * self, const GstH266SPS * sps, gint max_dpb_size)
{
  GstH266Decoder *h266_decoder = GST_H266_DECODER (self);
  GstVaBaseDec *base = GST_VA_BASE_DEC (self);
  GstH266Profile profile = sps->profile_tier_level.profile_idc;
  VAProfile profiles[4];
  gint i = 0, j;

  /* 1. The profile directly specified by the SPS should always be the
     first choice. It is the exact one.
     2. The profile in the input caps may contain the compatible profile
     chosen by the upstream element. Upstream element such as the parse
     may already decide the best compatible profile for us. We also need
     to consider it as a choice. */

  for (j = 0; j < G_N_ELEMENTS (profile_map); j++) {
    if (profile_map[j].profile_idc == profile) {
      profiles[i++] = profile_map[j].va_profile;
      break;
    }
  }

  if (h266_decoder->input_state->caps
      && gst_caps_is_fixed (h266_decoder->input_state->caps)) {
    GstH266Profile compatible_profile = GST_H266_PROFILE_INVALID;
    GstStructure *structure;
    const gchar *profile_str;

    structure = gst_caps_get_structure (h266_decoder->input_state->caps, 0);

    profile_str = gst_structure_get_string (structure, "profile");
    if (profile_str)
      compatible_profile = gst_h266_profile_from_string (profile_str);

    if (compatible_profile != profile) {
      GST_INFO_OBJECT (self, "The upstream set the compatible profile %s, "
          "also consider it as a candidate.", profile_str);

      for (j = 0; j < G_N_ELEMENTS (profile_map); j++) {
        if (profile_map[j].profile_idc == compatible_profile) {
          profiles[i++] = profile_map[j].va_profile;
          break;
        }
      }
    }
  }

  for (j = 0; j < i && j < G_N_ELEMENTS (profiles); j++) {
    if (gst_va_decoder_has_profile (base->decoder, profiles[j]))
      return profiles[j];
  }

  GST_ERROR_OBJECT (self, "Unsupported profile: %d", profile);

  return VAProfileNone;
}

static guint
_get_rtformat (GstVaH266Dec * self, guint8 bit_depth, guint8 chroma_format_idc)
{
  switch (bit_depth) {
    case 11:
    case 12:
      if (chroma_format_idc == 3)
        return VA_RT_FORMAT_YUV444_12;
      if (chroma_format_idc == 2)
        return VA_RT_FORMAT_YUV422_12;
      else
        return VA_RT_FORMAT_YUV420_12;
      break;
    case 9:
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
      GST_ERROR_OBJECT (self, "Unsupported chroma format: %d with "
          "bit depth: %d", chroma_format_idc, bit_depth);
      return 0;
  }
}

static GstFlowReturn
gst_va_h266_dec_new_sequence (GstH266Decoder * decoder, const GstH266SPS * sps,
    gint max_dpb_size)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaH266Dec *self = GST_VA_H266_DEC (decoder);
  GstVideoInfo *info = &base->output_info;
  VAProfile profile;
  guint rt_format;
  gint display_width, display_height;
  gint padding_left, padding_right, padding_top, padding_bottom;
  gboolean negotiation_needed = FALSE;

  if (sps->conformance_window_flag) {
    display_width = sps->crop_rect_width;
    display_height = sps->crop_rect_height;
    padding_left = sps->crop_rect_x;
    padding_right = sps->max_width - sps->crop_rect_x - display_width;
    padding_top = sps->crop_rect_y;
    padding_bottom = sps->max_height - sps->crop_rect_y - display_height;
  } else {
    display_width = sps->max_width;
    display_height = sps->max_height;
    padding_left = padding_right = padding_top = padding_bottom = 0;
  }

  if (self->dpb_size < max_dpb_size)
    self->dpb_size = max_dpb_size;

  profile = _get_profile (self, sps, max_dpb_size);
  if (profile == VAProfileNone)
    return GST_FLOW_NOT_NEGOTIATED;

  rt_format =
      _get_rtformat (self, sps->bitdepth_minus8 + 8, sps->chroma_format_idc);
  if (rt_format == 0)
    return GST_FLOW_NOT_NEGOTIATED;

  if (!gst_va_decoder_config_is_equal (base->decoder, profile,
          rt_format, sps->max_width, sps->max_height)) {
    base->profile = profile;
    base->rt_format = rt_format;
    base->width = sps->max_width;
    base->height = sps->max_height;

    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Format changed to %s [%x] (%dx%d)",
        gst_va_profile_name (profile), rt_format, base->width, base->height);
  }

  if (GST_VIDEO_INFO_WIDTH (info) != display_width ||
      GST_VIDEO_INFO_HEIGHT (info) != display_height) {
    GST_VIDEO_INFO_WIDTH (info) = display_width;
    GST_VIDEO_INFO_HEIGHT (info) = display_height;

    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Resolution changed to %dx%d",
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
  }

  base->need_valign = GST_VIDEO_INFO_WIDTH (info) < base->width ||
      GST_VIDEO_INFO_HEIGHT (info) < base->height;
  if (base->need_valign) {
    /* *INDENT-OFF* */
    if (base->valign.padding_left != padding_left ||
        base->valign.padding_right != padding_right ||
        base->valign.padding_top != padding_top ||
        base->valign.padding_bottom != padding_bottom) {
      negotiation_needed = TRUE;
      GST_INFO_OBJECT (self, "crop rect changed to (%d,%d)-->(%d,%d)",
          padding_left, padding_top, padding_right, padding_bottom);
    }
    base->valign = (GstVideoAlignment) {
      .padding_left = padding_left,
      .padding_right = padding_right,
      .padding_top = padding_top,
      .padding_bottom = padding_bottom,
    };
    /* *INDENT-ON* */
  }

  base->min_buffers = self->dpb_size + 4;       /* dpb size + scratch surfaces */
  base->need_negotiation = negotiation_needed;
  g_clear_pointer (&base->input_state, gst_video_codec_state_unref);
  base->input_state = gst_video_codec_state_ref (decoder->input_state);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_va_h266_dec_new_picture (GstH266Decoder * decoder,
    GstVideoCodecFrame * frame, GstH266Picture * picture)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaH266Dec *self = GST_VA_H266_DEC (decoder);
  GstVaDecodePicture *pic;
  GstVideoInfo *info = &base->output_info;
  gint display_width, display_height;
  gint padding_left, padding_right, padding_top, padding_bottom;
  guint sps_max_width = base->width;
  guint sps_max_height = base->height;
  gboolean need_valign = FALSE;
  GstFlowReturn ret;

  if (picture->pps_width > sps_max_width ||
      picture->pps_height > sps_max_height) {
    GST_ERROR_OBJECT (self, "PPS resolution %dx%d is bigger than SPS "
        "resolution %dx%d", picture->pps_width, picture->pps_height,
        sps_max_width, sps_max_height);
    return GST_FLOW_ERROR;
  }

  if (!picture->pps_conformance_window_flag) {
    display_width = picture->pps_width;
    display_height = picture->pps_height;
    padding_left = 0;
    padding_right = sps_max_width - display_width;
    padding_top = 0;
    padding_bottom = sps_max_height - display_height;
  } else {
    display_width = picture->pps_crop_rect_width;
    display_height = picture->pps_crop_rect_height;
    padding_left = picture->pps_crop_rect_x;
    padding_right = sps_max_width - picture->pps_crop_rect_x - display_width;
    padding_top = picture->pps_crop_rect_y;
    padding_bottom = sps_max_height - picture->pps_crop_rect_y - display_height;
  }

  if (GST_VIDEO_INFO_WIDTH (info) != display_width ||
      GST_VIDEO_INFO_HEIGHT (info) != display_height) {
    GST_VIDEO_INFO_WIDTH (info) = display_width;
    GST_VIDEO_INFO_HEIGHT (info) = display_height;

    base->need_negotiation = TRUE;
    GST_INFO_OBJECT (self, "PPS change resolution to %dx%d",
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
  }

  if (padding_left > 0 || padding_right > 0 ||
      padding_top > 0 || padding_bottom > 0)
    need_valign = TRUE;

  if (need_valign != base->need_valign) {
    base->need_negotiation = TRUE;
  } else if (base->need_valign) {
    if (padding_left != base->valign.padding_left ||
        padding_right != base->valign.padding_right ||
        padding_top != base->valign.padding_top ||
        padding_bottom != base->valign.padding_bottom)
      base->need_negotiation = TRUE;
  }

  if (base->need_negotiation) {
    /* *INDENT-OFF* */
    base->need_valign = need_valign;
    base->valign = (GstVideoAlignment) {
      .padding_left = padding_left,
      .padding_right = padding_right,
      .padding_top = padding_top,
      .padding_bottom = padding_bottom,
    };
    /* *INDENT-ON* */
  }

  ret = gst_va_base_dec_prepare_output_frame (base, frame);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Failed to allocated output buffer, return %s",
        gst_flow_get_name (ret));
    return ret;
  }

  pic = gst_va_decode_picture_new (base->decoder, frame->output_buffer);

  gst_h266_picture_set_user_data (picture, pic,
      (GDestroyNotify) gst_va_decode_picture_free);

  GST_LOG_OBJECT (self, "New va decode picture %p - %#x", pic,
      gst_va_decode_picture_get_surface (pic));

  return GST_FLOW_OK;
}

static void
_init_vaapi_pic (VAPictureVVC * va_picture)
{
  va_picture->picture_id = VA_INVALID_ID;
  va_picture->flags = VA_PICTURE_VVC_INVALID;
  va_picture->pic_order_cnt = 0;
}

static void
_fill_vaapi_pic (GstH266Decoder * decoder, VAPictureVVC * va_picture,
    GstH266Picture * picture)
{
  GstVaDecodePicture *va_pic;

  va_pic = gst_h266_picture_get_user_data (picture);

  if (!va_pic) {
    _init_vaapi_pic (va_picture);
    return;
  }

  va_picture->picture_id = gst_va_decode_picture_get_surface (va_pic);
  va_picture->pic_order_cnt = picture->pic_order_cnt;
  va_picture->flags = 0;

  if (picture->ref && picture->long_term)
    va_picture->flags |= VA_PICTURE_VVC_LONG_TERM_REFERENCE;
}

static void
_fill_vaapi_reference_frames (GstH266Decoder * decoder,
    VAPictureParameterBufferVVC * pic_param, GstH266Dpb * dpb)
{
  GArray *ref_list = gst_h266_dpb_get_pictures_all (dpb);
  guint i, j;

  i = 0;
  for (j = 0; j < ref_list->len; j++) {
    GstH266Picture *pic = g_array_index (ref_list, GstH266Picture *, j);

    if (pic->ref) {
      if (i == 15) {
        GST_WARNING_OBJECT (decoder, "Too may refererence frame in DPB.");
        break;
      }

      _fill_vaapi_pic (decoder, &pic_param->ReferenceFrames[i], pic);
      i++;
    }
  }
  g_array_unref (ref_list);

  for (; i < 15; i++)
    _init_vaapi_pic (&pic_param->ReferenceFrames[i]);
}

static gboolean
_fill_vaapi_subpicture (GstVaH266Dec * self, GstH266SPS * sps, GstH266PPS * pps,
    GstVaDecodePicture * va_pic)
{
  GstVaBaseDec *base = &self->parent;
  guint16 SubpicIdVal;
  guint i;

  if (!sps->subpic_info_present_flag)
    return TRUE;

  for (i = 0; i <= sps->num_subpics_minus1; i++) {
    if (sps->subpic_id_mapping_explicitly_signalled_flag) {
      SubpicIdVal = pps->subpic_id_mapping_present_flag ?
          pps->subpic_id[i] : sps->subpic_id[i];
    } else {
      SubpicIdVal = i;
    }

    /* *INDENT-OFF* */
    VASubPicVVC subpic_param = {
      .sps_subpic_ctu_top_left_x = sps->subpic_ctu_top_left_x[i],
      .sps_subpic_ctu_top_left_y = sps->subpic_ctu_top_left_y[i],
      .sps_subpic_width_minus1 = sps->subpic_width_minus1[i],
      .sps_subpic_height_minus1 = sps->subpic_height_minus1[i],
      .SubpicIdVal = SubpicIdVal,
      .subpic_flags.bits = {
        .sps_subpic_treated_as_pic_flag = sps->subpic_treated_as_pic_flag[i],
        .sps_loop_filter_across_subpic_enabled_flag =
          sps->loop_filter_across_subpic_enabled_flag[i],
      }
    };
    /* *INDENT-ON* */

    if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
            VASubPicBufferType, &subpic_param, sizeof (VASubPicVVC)))
      return FALSE;
  }

  return TRUE;
}

static gboolean
_fill_vaapi_alf_aps (GstVaH266Dec * self,
    GstH266APS * aps, GstVaDecodePicture * va_pic)
{
  GstVaBaseDec *base = &self->parent;
  const GstH266ALF *alf = &aps->alf;
  gint sfIdx, filtIdx, altIdx, j, k;
  gint8 filtCoeff[GST_H266_NUM_ALF_FILTERS][12];

  /* *INDENT-OFF* */
  VAAlfDataVVC alf_param = {
    .aps_adaptation_parameter_set_id = aps->aps_id,
    .alf_luma_num_filters_signalled_minus1 = alf->luma_filter_signal_flag ?
      alf->luma_num_filters_signalled_minus1 : 0,
    .alf_chroma_num_alt_filters_minus1 = alf->chroma_filter_signal_flag ?
      alf->chroma_num_alt_filters_minus1 : 0,
    .alf_cc_cb_filters_signalled_minus1 = alf->cc_cb_filter_signal_flag ?
      alf->cc_cb_filters_signalled_minus1 : 0,
    .alf_cc_cr_filters_signalled_minus1 = alf->cc_cr_filter_signal_flag ?
      alf->cc_cr_filters_signalled_minus1 : 0,
    .alf_flags.bits = {
      .alf_luma_filter_signal_flag = alf->luma_filter_signal_flag,
      .alf_chroma_filter_signal_flag = alf->chroma_filter_signal_flag,
      .alf_cc_cb_filter_signal_flag = alf->cc_cb_filter_signal_flag,
      .alf_cc_cr_filter_signal_flag = alf->cc_cr_filter_signal_flag,
      .alf_luma_clip_flag = alf->luma_clip_flag,
      .alf_chroma_clip_flag = alf->chroma_clip_flag,
    }
  };
  /* *INDENT-ON* */

  /* Luma coeff */
  for (sfIdx = 0; sfIdx <= alf->luma_num_filters_signalled_minus1; sfIdx++) {
    for (j = 0; j < 12; j++) {
      filtCoeff[sfIdx][j] = alf->luma_coeff_abs[sfIdx][j] *
          (1 - 2 * alf->luma_coeff_sign[sfIdx][j]);
    }
  }

  for (filtIdx = 0; filtIdx < 25; filtIdx++) {
    alf_param.alf_luma_coeff_delta_idx[filtIdx] =
        alf->luma_coeff_delta_idx[filtIdx];

    for (j = 0; j < 12; j++) {
      if (filtIdx <= alf->luma_num_filters_signalled_minus1) {
        alf_param.filtCoeff[filtIdx][j] = filtCoeff[filtIdx][j];
        alf_param.alf_luma_clip_idx[filtIdx][j] =
            alf->luma_clip_idx[filtIdx][j];
      } else {
        alf_param.filtCoeff[filtIdx][j] = 0;
        alf_param.alf_luma_clip_idx[filtIdx][j] = 0;
      }
    }
  }

  /* chroma coeff */
  for (altIdx = 0; altIdx <= alf->chroma_num_alt_filters_minus1; altIdx++) {
    for (j = 0; j < 6; j++) {
      alf_param.AlfCoeffC[altIdx][j] = alf->chroma_coeff_abs[altIdx][j] *
          (1 - 2 * alf->chroma_coeff_sign[altIdx][j]);
      alf_param.alf_chroma_clip_idx[altIdx][j] =
          alf->chroma_clip_idx[altIdx][j];
    }
  }

  /* cb cr coeff */
  for (k = 0; k <= alf->cc_cb_filters_signalled_minus1; k++) {
    for (j = 0; j < 7; j++) {
      if (alf->cc_cb_mapped_coeff_abs[k][j]) {
        alf_param.CcAlfApsCoeffCb[k][j] =
            (1 - 2 * alf->cc_cb_coeff_sign[k][j]) *
            (1 << (alf->cc_cb_mapped_coeff_abs[k][j] - 1));
      } else {
        alf_param.CcAlfApsCoeffCb[k][j] = 0;
      }
    }
  }

  for (k = 0; k <= alf->cc_cr_filters_signalled_minus1; k++) {
    for (j = 0; j < 7; j++) {
      if (alf->cc_cr_mapped_coeff_abs[k][j]) {
        alf_param.CcAlfApsCoeffCr[k][j] =
            (1 - 2 * alf->cc_cr_coeff_sign[k][j]) *
            (1 << (alf->cc_cr_mapped_coeff_abs[k][j] - 1));
      } else {
        alf_param.CcAlfApsCoeffCr[k][j] = 0;
      }
    }
  }

  if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
          VAAlfBufferType, &alf_param, sizeof (VAAlfDataVVC)))
    return FALSE;

  return TRUE;
}

static gboolean
_fill_vaapi_lmcs_aps (GstVaH266Dec * self,
    GstH266APS * aps, GstVaDecodePicture * va_pic)
{
  GstVaBaseDec *base = &self->parent;
  const GstH266LMCS *lmcs = &aps->lmcs;
  gint i;

  VALmcsDataVVC lmcs_param = {
    .aps_adaptation_parameter_set_id = aps->aps_id,
    .lmcs_min_bin_idx = lmcs->min_bin_idx,
    .lmcs_delta_max_bin_idx = lmcs->delta_max_bin_idx,
    .lmcsDeltaCrs = (1 - 2 * lmcs->delta_sign_crs_flag) * lmcs->delta_abs_crs,
  };

  for (i = 0; i < 16; i++)
    lmcs_param.lmcsDeltaCW[i] =
        (1 - 2 * lmcs->delta_sign_cw_flag[i]) * lmcs->delta_abs_cw[i];

  if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
          VALmcsBufferType, &lmcs_param, sizeof (VALmcsDataVVC)))
    return FALSE;

  return TRUE;
}

static gboolean
_fill_vaapi_scaling_list_aps (GstVaH266Dec * self,
    GstH266APS * aps, GstVaDecodePicture * va_pic)
{
  GstVaBaseDec *base = &self->parent;
  const GstH266ScalingList *sl = &aps->sl;
  gint i, j, k;

  VAScalingListVVC sl_param = {
    .aps_adaptation_parameter_set_id = aps->aps_id,
  };

  for (i = 0; i < 14; i++)
    sl_param.ScalingMatrixDCRec[i] = sl->scaling_list_DC[i];

  for (i = 0; i < 2; i++)
    for (j = 0; j < 2; j++)
      for (k = 0; k < 2; k++)
        sl_param.ScalingMatrixRec2x2[i][j][k] = sl->scaling_list[i][k * 2 + j];

  for (i = 2; i < 8; i++)
    for (j = 0; j < 4; j++)
      for (k = 0; k < 4; k++)
        sl_param.ScalingMatrixRec4x4[i - 2][j][k] =
            sl->scaling_list[i][k * 4 + j];

  for (i = 8; i < 28; i++)
    for (j = 0; j < 8; j++)
      for (k = 0; k < 8; k++)
        sl_param.ScalingMatrixRec8x8[i - 8][j][k] =
            sl->scaling_list[i][k * 8 + j];

  if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
          VAIQMatrixBufferType, &sl_param, sizeof (VAScalingListVVC)))
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_va_h266_dec_start_picture (GstH266Decoder * decoder,
    GstH266Picture * picture, GstH266Slice * slice, GstH266Dpb * dpb)
{
  GstVaH266Dec *self = GST_VA_H266_DEC (decoder);
  GstVaBaseDec *base = &self->parent;
  GstVaDecodePicture *va_pic;
  VAPictureParameterBufferVVC *pic_param = &self->pic_param;
  GstH266SPS *sps;
  GstH266PPS *pps;
  GstH266APS *aps;
  GstH266PicHdr *ph;
  guint16 tile_dim;
  gint i, j;

  va_pic = gst_h266_picture_get_user_data (picture);

  ph = &slice->header.picture_header;
  pps = ph->pps;
  sps = pps->sps;

  /* *INDENT-OFF* */
#define F(S, FIELD)  .G_PASTE(G_PASTE(S, _), FIELD)=S->FIELD
  *pic_param = (VAPictureParameterBufferVVC) {
    .pps_pic_width_in_luma_samples  = pps->width,
    .pps_pic_height_in_luma_samples = pps->height,
    F(sps, num_subpics_minus1),
    F(sps, chroma_format_idc),
    F(sps, bitdepth_minus8),
    F(sps, log2_ctu_size_minus5),
    F(sps, log2_min_luma_coding_block_size_minus2),
    F(sps, log2_transform_skip_max_size_minus2),
    F(sps, six_minus_max_num_merge_cand),
    F(sps, five_minus_max_num_subblock_merge_cand),
    F(sps, max_num_merge_cand_minus_max_num_gpm_cand),
    F(sps, log2_parallel_merge_level_minus2),
    F(sps, min_qp_prime_ts),
    .sps_six_minus_max_num_ibc_merge_cand = sps->six_minus_max_num_ibc_merge_cand,
    .sps_num_ladf_intervals_minus2 =
      sps->ladf_enabled_flag ? sps->num_ladf_intervals_minus2 : 0,
    F(sps, ladf_lowest_interval_qp_offset),
    .sps_flags.bits = {
      F(sps, subpic_info_present_flag),
      F(sps, independent_subpics_flag),
      F(sps, subpic_same_size_flag),
      F(sps, entropy_coding_sync_enabled_flag),
      F(sps, qtbtt_dual_tree_intra_flag),
      F(sps, max_luma_transform_size_64_flag),
      F(sps, transform_skip_enabled_flag),
      F(sps, bdpcm_enabled_flag),
      F(sps, mts_enabled_flag),
      F(sps, explicit_mts_intra_enabled_flag),
      F(sps, explicit_mts_inter_enabled_flag),
      F(sps, lfnst_enabled_flag),
      F(sps, joint_cbcr_enabled_flag),
      F(sps, same_qp_table_for_chroma_flag),
      F(sps, sao_enabled_flag),
      F(sps, alf_enabled_flag),
      F(sps, ccalf_enabled_flag),
      F(sps, lmcs_enabled_flag),
      F(sps, sbtmvp_enabled_flag),
      F(sps, amvr_enabled_flag),
      F(sps, smvd_enabled_flag),
      F(sps, mmvd_enabled_flag),
      F(sps, sbt_enabled_flag ),
      F(sps, affine_enabled_flag),
      .sps_6param_affine_enabled_flag = sps->sps_6param_affine_enabled_flag,
      F(sps, affine_amvr_enabled_flag),
      F(sps, affine_prof_enabled_flag),
      F(sps, bcw_enabled_flag),
      F(sps, ciip_enabled_flag),
      F(sps, gpm_enabled_flag),
      F(sps, isp_enabled_flag),
      F(sps, mrl_enabled_flag),
      F(sps, mip_enabled_flag),
      F(sps, cclm_enabled_flag),
      F(sps, chroma_horizontal_collocated_flag),
      F(sps, chroma_vertical_collocated_flag),
      F(sps, palette_enabled_flag),
      F(sps, act_enabled_flag),
      F(sps, ibc_enabled_flag),
      F(sps, ladf_enabled_flag),
      F(sps, explicit_scaling_list_enabled_flag),
      F(sps, scaling_matrix_for_lfnst_disabled_flag),
      F(sps, scaling_matrix_for_alternative_colour_space_disabled_flag),
      F(sps, scaling_matrix_designated_colour_space_flag),
      F(sps, virtual_boundaries_enabled_flag),
      F(sps, virtual_boundaries_present_flag),
    },
    .NumVerVirtualBoundaries = sps->virtual_boundaries_present_flag ?
        sps->num_ver_virtual_boundaries : ph->num_ver_virtual_boundaries,
    .NumHorVirtualBoundaries = sps->virtual_boundaries_present_flag ?
        sps->num_hor_virtual_boundaries : ph->num_hor_virtual_boundaries,
    F(pps, scaling_win_left_offset),
    F(pps, scaling_win_right_offset),
    F(pps, scaling_win_top_offset),
    F(pps, scaling_win_bottom_offset),
    .pps_num_exp_tile_columns_minus1 =
      pps->no_pic_partition_flag ? 0 : pps->num_exp_tile_columns_minus1,
    .pps_num_exp_tile_rows_minus1 =
      pps->no_pic_partition_flag ? 0 : pps->num_exp_tile_rows_minus1,
    .pps_num_slices_in_pic_minus1 =
      pps->no_pic_partition_flag ? 0 : pps->num_slices_in_pic_minus1,
    F(pps, pic_width_minus_wraparound_offset),
    F(pps, cb_qp_offset),
    F(pps, cr_qp_offset),
    F(pps, joint_cbcr_qp_offset_value),
    F(pps, chroma_qp_offset_list_len_minus1),
    .pps_flags.bits = {
      F(pps, loop_filter_across_tiles_enabled_flag),
      F(pps, rect_slice_flag),
      F(pps, single_slice_per_subpic_flag),
      F(pps, loop_filter_across_slices_enabled_flag),
      F(pps, weighted_pred_flag),
      F(pps, weighted_bipred_flag),
      F(pps, ref_wraparound_enabled_flag),
      F(pps, cu_qp_delta_enabled_flag),
      F(pps, cu_chroma_qp_offset_list_enabled_flag),
      F(pps, deblocking_filter_override_enabled_flag),
      F(pps, deblocking_filter_disabled_flag),
      F(pps, dbf_info_in_ph_flag),
      F(pps, sao_info_in_ph_flag),
      F(pps, alf_info_in_ph_flag),
    },
    F(ph, lmcs_aps_id),
    F(ph, scaling_list_aps_id),
    F(ph, log2_diff_min_qt_min_cb_intra_slice_luma),
    F(ph, max_mtt_hierarchy_depth_intra_slice_luma),
    F(ph, log2_diff_max_bt_min_qt_intra_slice_luma),
    F(ph, log2_diff_max_tt_min_qt_intra_slice_luma),
    F(ph, log2_diff_min_qt_min_cb_intra_slice_chroma),
    F(ph, max_mtt_hierarchy_depth_intra_slice_chroma),
    F(ph, log2_diff_max_bt_min_qt_intra_slice_chroma),
    F(ph, log2_diff_max_tt_min_qt_intra_slice_chroma),
    F(ph, cu_qp_delta_subdiv_intra_slice),
    F(ph, cu_chroma_qp_offset_subdiv_intra_slice),
    F(ph, log2_diff_min_qt_min_cb_inter_slice),
    F(ph, max_mtt_hierarchy_depth_inter_slice),
    F(ph, log2_diff_max_bt_min_qt_inter_slice),
    F(ph, log2_diff_max_tt_min_qt_inter_slice),
    F(ph, cu_qp_delta_subdiv_inter_slice),
    F(ph, cu_chroma_qp_offset_subdiv_inter_slice),
    .ph_flags.bits= {
      F(ph, non_ref_pic_flag),
      F(ph, alf_enabled_flag),
      F(ph, alf_cb_enabled_flag),
      F(ph, alf_cr_enabled_flag),
      F(ph, alf_cc_cb_enabled_flag),
      F(ph, alf_cc_cr_enabled_flag),
      F(ph, lmcs_enabled_flag),
      F(ph, chroma_residual_scale_flag),
      F(ph, explicit_scaling_list_enabled_flag),
      F(ph, virtual_boundaries_present_flag),
      F(ph, temporal_mvp_enabled_flag),
      F(ph, mmvd_fullpel_only_flag),
      F(ph, mvd_l1_zero_flag),
      F(ph, bdof_disabled_flag),
      F(ph, dmvr_disabled_flag),
      F(ph, prof_disabled_flag),
      F(ph, joint_cbcr_sign_flag),
      F(ph, sao_luma_enabled_flag),
      F(ph, sao_chroma_enabled_flag),
      F(ph, deblocking_filter_disabled_flag),
    },
    .PicMiscFlags.fields = {
      .IntraPicFlag = GST_H266_IS_NAL_TYPE_IRAP(slice->nalu.type),
    }
  };
#undef F
  /* *INDENT-ON* */

  _fill_vaapi_pic (decoder, &pic_param->CurrPic, picture);
  _fill_vaapi_reference_frames (decoder, pic_param, dpb);

  for (i = 0; i < GST_H266_MAX_SAMPLE_ARRAYS; i++)
    for (j = 0; j < GST_H266_MAX_POINTS_IN_QP_TABLE; j++)
      pic_param->ChromaQpTable[i][j] = sps->chroma_qp_table[i][j];

  for (i = 0; i < 4; i++) {
    pic_param->sps_ladf_qp_offset[i] = sps->ladf_qp_offset[i];
    pic_param->sps_ladf_delta_threshold_minus1[i] =
        sps->ladf_delta_threshold_minus1[i];
  }

  for (i = 0;
      i < (sps->virtual_boundaries_present_flag ?
          sps->num_ver_virtual_boundaries : ph->num_ver_virtual_boundaries);
      i++) {
    pic_param->VirtualBoundaryPosX[i] = (sps->virtual_boundaries_present_flag ?
        (sps->virtual_boundary_pos_x_minus1[i] + 1) :
        (ph->virtual_boundary_pos_x_minus1[i] + 1)) * 8;
  }
  for (i = 0;
      i < (sps->virtual_boundaries_present_flag ?
          sps->num_hor_virtual_boundaries : ph->num_hor_virtual_boundaries);
      i++) {
    pic_param->VirtualBoundaryPosY[i] =
        (sps->virtual_boundaries_present_flag ?
        (sps->virtual_boundary_pos_y_minus1[i] + 1) :
        (ph->virtual_boundary_pos_y_minus1[i] + 1)) * 8;
  }

  for (i = 0; i < 6; i++) {
    pic_param->pps_cb_qp_offset_list[i] = pps->cb_qp_offset_list[i];
    pic_param->pps_cr_qp_offset_list[i] = pps->cr_qp_offset_list[i];
    pic_param->pps_joint_cbcr_qp_offset_list[i] =
        pps->joint_cbcr_qp_offset_list[i];
  }

  if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
          VAPictureParameterBufferType, pic_param,
          sizeof (VAPictureParameterBufferVVC)))
    return GST_FLOW_ERROR;

  if (!_fill_vaapi_subpicture (self, sps, pps, va_pic))
    return GST_FLOW_ERROR;

  for (i = 0; i < decoder->aps_list[GST_H266_ALF_APS]->len; i++) {
    aps = g_array_index (decoder->aps_list[GST_H266_ALF_APS], GstH266APS *, i);
    if (!_fill_vaapi_alf_aps (self, aps, va_pic))
      return GST_FLOW_ERROR;
  }
  for (i = 0; i < decoder->aps_list[GST_H266_LMCS_APS]->len; i++) {
    aps = g_array_index (decoder->aps_list[GST_H266_LMCS_APS], GstH266APS *, i);
    if (!_fill_vaapi_lmcs_aps (self, aps, va_pic))
      return GST_FLOW_ERROR;
  }
  for (i = 0; i < decoder->aps_list[GST_H266_SCALING_APS]->len; i++) {
    aps = g_array_index (decoder->aps_list[GST_H266_SCALING_APS],
        GstH266APS *, i);
    if (!_fill_vaapi_scaling_list_aps (self, aps, va_pic))
      return GST_FLOW_ERROR;
  }

  /* Tile buffer */
  for (i = 0; i <= pps->num_exp_tile_columns_minus1; i++) {
    tile_dim = pps->tile_column_width_minus1[i];

    if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
            VATileBufferType, &tile_dim, sizeof (tile_dim)))
      return GST_FLOW_ERROR;
  }
  for (i = 0; i <= pps->num_exp_tile_rows_minus1; i++) {
    tile_dim = pps->tile_row_height_minus1[i];

    if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
            VATileBufferType, &tile_dim, sizeof (tile_dim)))
      return GST_FLOW_ERROR;
  }

  /* Slice Struct buffer */
  if (!pps->no_pic_partition_flag && pps->rect_slice_flag) {
    for (i = 0; i <= pps->num_slices_in_pic_minus1; i++) {
      VASliceStructVVC ss_param = {
        .SliceTopLeftTileIdx = pps->slice_top_left_tile_idx[i],
        .pps_slice_width_in_tiles_minus1 = pps->slice_width_in_tiles_minus1[i],
        .pps_slice_height_in_tiles_minus1 =
            pps->slice_height_in_tiles_minus1[i],
      };

      if (pps->slice_width_in_tiles_minus1[i] > 0
          || pps->slice_height_in_tiles_minus1[i] > 0)
        ss_param.pps_exp_slice_height_in_ctus_minus1 = 0;
      else {
        ss_param.pps_exp_slice_height_in_ctus_minus1 =
            pps->slice_height_in_ctus[i] ? pps->slice_height_in_ctus[i] - 1 : 0;
      }

      if (!gst_va_decoder_add_param_buffer (base->decoder, va_pic,
              VASliceStructBufferType, &ss_param, sizeof (VASliceStructVVC)))
        return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_va_h266_dec_end_picture (GstH266Decoder * decoder, GstH266Picture * picture)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaDecodePicture *va_pic;

  GST_LOG_OBJECT (base, "end picture %p, (poc %d)",
      picture, picture->pic_order_cnt);

  va_pic = gst_h266_picture_get_user_data (picture);

  if (!gst_va_decoder_decode (base->decoder, va_pic))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_va_h266_dec_output_picture (GstH266Decoder * decoder,
    GstVideoCodecFrame * frame, GstH266Picture * picture)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaH266Dec *self = GST_VA_H266_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  gboolean ret;

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->pic_order_cnt);

  ret = gst_va_base_dec_process_output (base, frame,
      GST_CODEC_PICTURE (picture)->discont_state, picture->buffer_flags);
  gst_h266_picture_unref (picture);

  if (ret)
    return gst_video_decoder_finish_frame (vdec, frame);
  return GST_FLOW_ERROR;
}

static inline guint
_get_slice_data_bit_offset (GstH266SliceHdr * slice_hdr, guint nal_header_bytes)
{
  guint epb_count;

  epb_count = slice_hdr->n_emulation_prevention_bytes;
  return nal_header_bytes + (slice_hdr->header_size + 7) / 8 - epb_count;
}

static void
_fill_ref_pic_index (GstH266Decoder * decoder,
    VASliceParameterBufferVVC * slice_param, guint list)
{
  GstVaH266Dec *self = GST_VA_H266_DEC (decoder);
  const VAPictureParameterBufferVVC *pic_param = &self->pic_param;
  const VAPictureVVC *va_picture;
  gint index, i, poc;

  for (index = 0; index < decoder->NumRefIdxActive[list]; index++) {
    if (!decoder->RefPicList[list][index]) {
      GST_WARNING_OBJECT (decoder, "Reference of list%d index %d is missing",
          list, index);
      slice_param->RefPicList[list][index] = 0xFF;
      continue;
    }

    poc = decoder->RefPicPocList[list][index];
    if (poc == G_MININT32)
      poc = decoder->RefPicLtPocList[list][index];

    for (i = 0; i < 15; i++) {
      va_picture = &pic_param->ReferenceFrames[i];
      if (va_picture->picture_id == VA_INVALID_ID)
        continue;

      if (va_picture->pic_order_cnt == poc)
        break;
    }
    if (i < 15) {
      slice_param->RefPicList[list][index] = i;
    } else {
      GST_WARNING_OBJECT (decoder, "Reference of list%d index %d with POC %d "
          "is missing", list, index, poc);
      slice_param->RefPicList[list][index] = 0xFF;
    }
  }
}

static GstFlowReturn
gst_va_h266_dec_decode_slice (GstH266Decoder * decoder,
    GstH266Picture * picture, GstH266Slice * slice)
{
  GstVaH266Dec *self = GST_VA_H266_DEC (decoder);
  GstVaBaseDec *base = &self->parent;
  GstH266SliceHdr *sh = &slice->header;
  GstVaDecodePicture *va_pic;
  GstH266NalUnit *nalu = &slice->nalu;
  VASliceParameterBufferVVC slice_param;
  gint i, j;

  va_pic = gst_h266_picture_get_user_data (picture);

  /* *INDENT-OFF* */
#define F(S, FIELD)  .G_PASTE(G_PASTE(S, _), FIELD)=S->FIELD
  slice_param = (VASliceParameterBufferVVC) {
    .slice_data_size = nalu->size,
    .slice_data_offset = 0,
    .slice_data_flag = VA_SLICE_DATA_FLAG_ALL,
    .slice_data_byte_offset = _get_slice_data_bit_offset (sh, nalu->header_bytes),
    F(sh, subpic_id),
    F(sh, slice_address),
    F(sh, num_tiles_in_slice_minus1),
    F(sh, slice_type),
    F(sh, num_alf_aps_ids_luma),
    F(sh, alf_aps_id_chroma),
    F(sh, alf_cc_cb_aps_id),
    F(sh, alf_cc_cr_aps_id),
    .NumRefIdxActive[0] = sh->num_ref_idx_active[0],
    .NumRefIdxActive[1] = sh->num_ref_idx_active[1],
    F(sh, collocated_ref_idx),
    .SliceQpY = sh->slice_qp_y,
    F(sh, cb_qp_offset),
    F(sh, cr_qp_offset),
    F(sh, joint_cbcr_qp_offset),
    F(sh, luma_beta_offset_div2),
    F(sh, luma_tc_offset_div2),
    F(sh, cb_beta_offset_div2),
    F(sh, cb_tc_offset_div2),
    F(sh, cr_beta_offset_div2),
    F(sh, cr_tc_offset_div2),
    .WPInfo = {
      .luma_log2_weight_denom = sh->pred_weight_table.luma_log2_weight_denom,
      .delta_chroma_log2_weight_denom =
        sh->pred_weight_table.delta_chroma_log2_weight_denom,
      .num_l0_weights = sh->pred_weight_table.num_l0_weights,
      .num_l1_weights = sh->pred_weight_table.num_l1_weights,
    },
    .sh_flags.bits = {
      F(sh, alf_enabled_flag),
      F(sh, alf_cb_enabled_flag),
      F(sh, alf_cr_enabled_flag),
      F(sh, alf_cc_cb_enabled_flag),
      F(sh, alf_cc_cr_enabled_flag),
      F(sh, lmcs_used_flag),
      F(sh, explicit_scaling_list_used_flag),
      F(sh, cabac_init_flag),
      F(sh, collocated_from_l0_flag),
      F(sh, cu_chroma_qp_offset_enabled_flag),
      F(sh, sao_luma_used_flag),
      F(sh, sao_chroma_used_flag),
      F(sh, deblocking_filter_disabled_flag),
      F(sh, dep_quant_used_flag),
      F(sh, sign_data_hiding_used_flag),
      F(sh, ts_residual_coding_disabled_flag),
    },
  };
#undef F
  /* *INDENT-ON* */

  for (i = 0; i < 7; i++)
    slice_param.sh_alf_aps_id_luma[i] = sh->alf_aps_id_luma[i];

  for (i = 0; i < 15; i++) {
    slice_param.WPInfo.luma_weight_l0_flag[i] =
        sh->pred_weight_table.luma_weight_l0_flag[i];
    slice_param.WPInfo.chroma_weight_l0_flag[i] =
        sh->pred_weight_table.chroma_weight_l0_flag[i];
    slice_param.WPInfo.delta_luma_weight_l0[i] =
        sh->pred_weight_table.delta_luma_weight_l0[i];

    slice_param.WPInfo.luma_offset_l0[i] =
        sh->pred_weight_table.luma_offset_l0[i];

    slice_param.WPInfo.luma_weight_l1_flag[i] =
        sh->pred_weight_table.luma_weight_l1_flag[i];
    slice_param.WPInfo.chroma_weight_l1_flag[i] =
        sh->pred_weight_table.chroma_weight_l1_flag[i];
    slice_param.WPInfo.delta_luma_weight_l1[i] =
        sh->pred_weight_table.delta_luma_weight_l1[i];

    slice_param.WPInfo.luma_offset_l1[i] =
        sh->pred_weight_table.luma_offset_l1[i];
  }

  for (i = 0; i < 15; i++) {
    for (j = 0; j < 2; j++) {
      slice_param.WPInfo.delta_chroma_weight_l0[i][j] =
          sh->pred_weight_table.delta_chroma_weight_l0[i][j];
      slice_param.WPInfo.delta_chroma_offset_l0[i][j] =
          sh->pred_weight_table.delta_chroma_offset_l0[i][j];

      slice_param.WPInfo.delta_chroma_weight_l1[i][j] =
          sh->pred_weight_table.delta_chroma_weight_l1[i][j];
      slice_param.WPInfo.delta_chroma_offset_l1[i][j] =
          sh->pred_weight_table.delta_chroma_offset_l1[i][j];
    }
  }

  memset (&slice_param.RefPicList, 0xFF, sizeof (slice_param.RefPicList));
  for (i = 0; i < (slice->header.slice_type == GST_H266_B_SLICE ? 2 :
          (slice->header.slice_type == GST_H266_P_SLICE ? 1 : 0)); i++)
    _fill_ref_pic_index (decoder, &slice_param, i);

  if (!gst_va_decoder_add_slice_buffer (base->decoder, va_pic, &slice_param,
          sizeof (slice_param), slice->nalu.data + slice->nalu.offset,
          slice->nalu.size)) {
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstCaps *
_complete_sink_caps (GstCaps * sinkcaps)
{
  GstCaps *caps = gst_caps_copy (sinkcaps);
  GValue val = G_VALUE_INIT;
  const gchar *streamformat[] = { "vvc1", "vvi1", "byte-stream" };
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

static void
gst_va_h266_dec_init (GTypeInstance * instance, gpointer g_class)
{
  gst_va_base_dec_init (GST_VA_BASE_DEC (instance), GST_CAT_DEFAULT);
}

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_h266dec_debug, "vah266dec", 0,
      "VA H266 decoder");

  return NULL;
}

static void
gst_va_h266_dec_dispose (GObject * object)
{
  gst_va_base_dec_close (GST_VIDEO_DECODER (object));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_va_h266_dec_getcaps (GstVideoDecoder * decoder, GstCaps * filter)
{
  GstCaps *sinkcaps, *caps = NULL, *tmp;
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);

  if (base->decoder)
    caps = gst_va_decoder_get_sinkpad_caps (base->decoder);

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
    GST_LOG_OBJECT (base, "Returning caps %" GST_PTR_FORMAT, caps);
  } else if (!caps) {
    caps = gst_video_decoder_proxy_getcaps (decoder, NULL, filter);
  }

  return caps;
}

static void
gst_va_h266_dec_class_init (gpointer g_class, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (g_class);
  GstH266DecoderClass *h266decoder_class = GST_H266_DECODER_CLASS (g_class);
  struct CData *cdata = class_data;
  gchar *long_name;

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API H.266 Decoder in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API H.266 Decoder");
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware", "VA-API based H.266 video decoder",
      "He Junyan <junyan.he@intel.com>");

  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  src_doc_caps = gst_caps_from_string (src_caps_str);

  parent_class = g_type_class_peek_parent (g_class);

  /**
   * GstVaH266Dec:device-path:
   *
   * It shows the DRM device path used for the VA operation, if any.
   *
   * Since: 1.26
   */
  gst_va_base_dec_class_init (GST_VA_BASE_DEC_CLASS (g_class), VVC,
      cdata->render_device_path, cdata->sink_caps, cdata->src_caps,
      src_doc_caps, sink_doc_caps);

  gobject_class->dispose = gst_va_h266_dec_dispose;

  decoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_va_h266_dec_getcaps);

  h266decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_va_h266_dec_new_sequence);
  h266decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_va_h266_dec_new_picture);
  h266decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_va_h266_dec_start_picture);
  h266decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_va_h266_dec_end_picture);
  h266decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_va_h266_dec_output_picture);
  h266decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_va_h266_dec_decode_slice);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);
}

gboolean
gst_va_h266_dec_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaH266DecClass),
    .class_init = gst_va_h266_dec_class_init,
    .instance_size = sizeof (GstVaH266Dec),
    .instance_init = gst_va_h266_dec_init,
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

  gst_va_create_feature_name (device, "GstVaH266Dec", "GstVa%sH266Dec",
      &type_name, "vah266dec", "va%sh266dec", &feature_name,
      &cdata->description, &rank);

  g_once (&debug_once, _register_debug_category, NULL);

  type = g_type_register_static (GST_TYPE_H266_DECODER,
      type_name, &type_info, 0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
