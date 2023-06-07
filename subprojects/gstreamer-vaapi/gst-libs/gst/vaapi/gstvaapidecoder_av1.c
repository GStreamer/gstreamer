/*
 *  gstvaapidecoder_av1.c - AV1 decoder
 *
 *  Copyright (C) 2019-2020 Intel Corporation
 *    Author: Junyan He <junyan.he@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidecoder_av1
 * @short_description: AV1 decoder
 */

#include "sysdeps.h"
#include <gst/codecparsers/gstav1parser.h>
#include "gstvaapidecoder_av1.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"

#include "gstvaapicompat.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_DECODER_AV1_CAST(decoder) ((GstVaapiDecoderAV1 *)(decoder))

typedef struct _GstVaapiDecoderAV1Private GstVaapiDecoderAV1Private;
typedef struct _GstVaapiDecoderAV1Class GstVaapiDecoderAV1Class;
typedef struct _GstVaapiPictureAV1 GstVaapiPictureAV1;

struct _GstVaapiDecoderAV1Private
{
  GstVaapiProfile profile;
  guint width;
  guint height;
  gboolean reset_context;
  GstVaapiPictureAV1 *current_picture;
  gboolean annex_b;
  GstAV1Parser *parser;
  GstAV1SequenceHeaderOBU *seq_header;
  GstVaapiPictureAV1 *ref_frames[GST_AV1_NUM_REF_FRAMES];
};

/**
 * GstVaapiDecoderAV1:
 *
 * A decoder based on AV1.
 */
struct _GstVaapiDecoderAV1
{
  /*< private > */
  GstVaapiDecoder parent_instance;
  GstVaapiDecoderAV1Private priv;
};

/**
 * GstVaapiDecoderAV1Class:
 *
 * A decoder class based on AV1.
 */
struct _GstVaapiDecoderAV1Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderAV1, gst_vaapi_decoder_av1,
    GST_TYPE_VAAPI_DECODER);

/* ------------------------------------------------------------------------- */
/* --- AV1 Parser Info                                                   --- */
/* ------------------------------------------------------------------------- */
typedef struct _GstVaapiParserInfoAV1 GstVaapiParserInfoAV1;
struct _GstVaapiParserInfoAV1
{
  GstVaapiMiniObject parent_instance;
  GstAV1OBU obu;
  union
  {
    GstAV1SequenceHeaderOBU seq_header;
    GstAV1MetadataOBU metadata;
    GstAV1FrameHeaderOBU frame_header;
    GstAV1TileListOBU tile_list;
    GstAV1TileGroupOBU tile_group;
    GstAV1FrameOBU frame;
  };
  /* The offset between input data and real OBU data */
  gint data_offset;
};

static void
parser_info_av1_finalize (GstVaapiParserInfoAV1 * pi)
{
}

static inline const GstVaapiMiniObjectClass *
parser_info_av1_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiParserInfoAV1Class = {
    .size = sizeof (GstVaapiParserInfoAV1),
    .finalize = (GDestroyNotify) parser_info_av1_finalize
  };
  return &GstVaapiParserInfoAV1Class;
}

static inline GstVaapiParserInfoAV1 *
parser_info_av1_new (GstAV1OBU * obu)
{
  GstVaapiParserInfoAV1 *pi = (GstVaapiParserInfoAV1 *)
      gst_vaapi_mini_object_new (parser_info_av1_class ());

  if (pi)
    pi->obu = *obu;

  return pi;
}

/* ------------------------------------------------------------------------- */
/* --- AV1 Picture                                                       --- */
/* ------------------------------------------------------------------------- */
struct _GstVaapiPictureAV1
{
  GstVaapiPicture base;
  /* When apply_grain enabled, recon proxy is different from the display
     proxy, otherwise the same. */
  GstVaapiSurfaceProxy *recon_proxy;
  GstAV1FrameHeaderOBU frame_header;
  gboolean cloned;
};

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiPictureAV1, gst_vaapi_picture_av1);

void
gst_vaapi_picture_av1_destroy (GstVaapiPictureAV1 * picture)
{
  if (picture->recon_proxy) {
    gst_vaapi_surface_proxy_unref (picture->recon_proxy);
    picture->recon_proxy = NULL;
  }
  gst_vaapi_picture_destroy (GST_VAAPI_PICTURE (picture));
}

gboolean
gst_vaapi_picture_av1_create (GstVaapiPictureAV1 * picture,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  if (!gst_vaapi_picture_create (GST_VAAPI_PICTURE (picture), args))
    return FALSE;

  picture->recon_proxy = gst_vaapi_surface_proxy_ref (picture->base.proxy);
  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE_ID (picture->recon_proxy) ==
      picture->base.surface_id);

  return TRUE;
}

static inline GstVaapiPictureAV1 *
gst_vaapi_picture_av1_new (GstVaapiDecoderAV1 * decoder)
{
  GstVaapiPictureAV1 *picture;

  picture = (GstVaapiPictureAV1 *)
      gst_vaapi_codec_object_new (&GstVaapiPictureAV1Class,
      GST_VAAPI_CODEC_BASE (decoder), NULL,
      sizeof (VADecPictureParameterBufferAV1), NULL, 0, 0);

  if (picture)
    picture->cloned = FALSE;

  return picture;
}

static const gchar *
av1_obu_name (GstAV1OBUType type)
{
  switch (type) {
    case GST_AV1_OBU_SEQUENCE_HEADER:
      return "sequence header";
    case GST_AV1_OBU_TEMPORAL_DELIMITER:
      return "temporal delimiter";
    case GST_AV1_OBU_FRAME_HEADER:
      return "frame header";
    case GST_AV1_OBU_TILE_GROUP:
      return "tile group";
    case GST_AV1_OBU_METADATA:
      return "metadata";
    case GST_AV1_OBU_FRAME:
      return "frame";
    case GST_AV1_OBU_REDUNDANT_FRAME_HEADER:
      return "redundant frame header";
    case GST_AV1_OBU_TILE_LIST:
      return "tile list";
    case GST_AV1_OBU_PADDING:
      return "padding";
    default:
      return "unknown";
  }

  return NULL;
}

static GstVaapiChromaType
av1_get_chroma_type (GstVaapiProfile profile,
    GstAV1SequenceHeaderOBU * seq_header)
{
  /* 6.4.1:
     seq_profile  Bit depth  Monochrome support  Chroma subsampling
     0            8 or 10    Yes                 YUV 4:2:0
     1            8 or 10    No                  YUV 4:4:4
     2            8 or 10    Yes                 YUV 4:2:2
     2            12         Yes                 YUV 4:2:0,YUV 4:2:2,YUV 4:4:4
   */

  /* TODO: consider Monochrome case. Just return 4:2:0 for Monochrome now. */
  switch (profile) {
    case GST_VAAPI_PROFILE_AV1_0:
      if (seq_header->bit_depth == 8) {
        return GST_VAAPI_CHROMA_TYPE_YUV420;
      } else if (seq_header->bit_depth == 10) {
        return GST_VAAPI_CHROMA_TYPE_YUV420_10BPP;
      }
      break;
    case GST_VAAPI_PROFILE_AV1_1:
      if (seq_header->bit_depth == 8) {
        return GST_VAAPI_CHROMA_TYPE_YUV444;
      } else if (seq_header->bit_depth == 10) {
        return GST_VAAPI_CHROMA_TYPE_YUV444_10BPP;
      }
      break;
    default:
      break;
  }

  GST_WARNING ("can not decide chrome type.");
  return 0;
}

static GstVaapiProfile
av1_get_profile (guint profile_idc)
{
  GstVaapiProfile profile;

  switch (profile_idc) {
    case GST_AV1_PROFILE_0:
      profile = GST_VAAPI_PROFILE_AV1_0;
      break;
    case GST_AV1_PROFILE_1:
      profile = GST_VAAPI_PROFILE_AV1_1;
      break;
    default:
      GST_INFO ("unsupported av1 profile_idc value %d", profile_idc);
      profile = GST_VAAPI_PROFILE_UNKNOWN;
      break;
  }
  return profile;
}

static GstVaapiDecoderStatus
av1_decode_seqeunce (GstVaapiDecoderAV1 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderAV1Private *const priv = &decoder->priv;
  GstVaapiProfile profile;
  GstVaapiParserInfoAV1 *const pi = unit->parsed_info;

  profile = av1_get_profile (pi->seq_header.seq_profile);
  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  if (!gst_vaapi_display_has_decoder (GST_VAAPI_DECODER_DISPLAY (decoder),
          profile, GST_VAAPI_ENTRYPOINT_VLD)) {
    GST_WARNING ("not supported av1 profile %s",
        gst_vaapi_profile_get_va_name (profile));
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  if (profile != priv->profile) {
    GST_DEBUG ("new av1 profile %s", gst_vaapi_profile_get_va_name (profile));
    /* We delay the context creation to when we know the frame resolution */
    priv->reset_context = TRUE;
    priv->profile = profile;
  }

  /* update the sequence */
  if (priv->seq_header)
    g_free (priv->seq_header);
  priv->seq_header =
      g_memdup2 (&pi->seq_header, sizeof (GstAV1SequenceHeaderOBU));

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
av1_decoder_ensure_context (GstVaapiDecoderAV1 * decoder)
{
  GstVaapiDecoderAV1Private *const priv = &decoder->priv;
  GstVaapiContextInfo info;

  if (priv->reset_context) {
    if (priv->current_picture)
      gst_vaapi_picture_replace (&priv->current_picture, NULL);

    /* *INDENT-OFF* */
    info = (GstVaapiContextInfo) {
      .profile = priv->profile,
      .entrypoint = GST_VAAPI_ENTRYPOINT_VLD,
      .chroma_type = av1_get_chroma_type (priv->profile, priv->seq_header),
      .width = priv->width,
      .height = priv->height,
      .ref_frames = GST_AV1_NUM_REF_FRAMES + 2,
    };
    /* *INDENT-ON* */

    if (!info.chroma_type)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;

    priv->reset_context = FALSE;
    if (!gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info)) {
      GST_WARNING ("can not make av1 decoder context with profile %s,"
          " width %d, height %d", gst_vaapi_profile_get_va_name (info.profile),
          info.width, info.height);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }
  }

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
av1_fill_segment_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header)
{
  guint i, j;
  uint8_t feature_mask;

#define COPY_SEG_FIELD(FP, FS) \
    pic_param->seg_info.segment_info_fields.bits.FP = \
        (frame_header)->segmentation_params.FS

  COPY_SEG_FIELD (enabled, segmentation_enabled);
  COPY_SEG_FIELD (update_map, segmentation_update_map);
  COPY_SEG_FIELD (temporal_update, segmentation_temporal_update);
  COPY_SEG_FIELD (update_data, segmentation_update_data);
  for (i = 0; i < GST_AV1_MAX_SEGMENTS; i++)
    for (j = 0; j < GST_AV1_SEG_LVL_MAX; j++)
      pic_param->seg_info.feature_data[i][j] =
          frame_header->segmentation_params.feature_data[i][j];

  for (i = 0; i < GST_AV1_MAX_SEGMENTS; i++) {
    feature_mask = 0;
    for (j = 0; j < GST_AV1_SEG_LVL_MAX; j++) {
      if (frame_header->segmentation_params.feature_enabled[i][j])
        feature_mask |= 1 << j;
    }
    pic_param->seg_info.feature_mask[i] = feature_mask;
  }
#undef COPY_SEG_FIELD
}

static void
av1_fill_film_grain_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header)
{
  guint i;

  if (!frame_header->film_grain_params.apply_grain) {
    memset (&pic_param->film_grain_info, 0, sizeof (VAFilmGrainStructAV1));
    return;
  }
#define COPY_FILM_GRAIN_FIELD(FP) \
    pic_param->SUB_FIELD.FP = (frame_header)->film_grain_params.FP
#define SUB_FIELD film_grain_info.film_grain_info_fields.bits

  COPY_FILM_GRAIN_FIELD (apply_grain);
  COPY_FILM_GRAIN_FIELD (chroma_scaling_from_luma);
  COPY_FILM_GRAIN_FIELD (grain_scaling_minus_8);
  COPY_FILM_GRAIN_FIELD (ar_coeff_lag);
  COPY_FILM_GRAIN_FIELD (ar_coeff_shift_minus_6);
  COPY_FILM_GRAIN_FIELD (grain_scale_shift);
  COPY_FILM_GRAIN_FIELD (overlap_flag);
  COPY_FILM_GRAIN_FIELD (clip_to_restricted_range);
#undef SUB_FIELD

  pic_param->film_grain_info.grain_seed =
      frame_header->film_grain_params.grain_seed;

  pic_param->film_grain_info.num_y_points =
      frame_header->film_grain_params.num_y_points;
  for (i = 0; i < frame_header->film_grain_params.num_y_points; i++) {
    pic_param->film_grain_info.point_y_value[i] =
        frame_header->film_grain_params.point_y_value[i];
    pic_param->film_grain_info.point_y_scaling[i] =
        frame_header->film_grain_params.point_y_scaling[i];
  }

  pic_param->film_grain_info.num_cb_points =
      frame_header->film_grain_params.num_cb_points;
  for (i = 0; i < frame_header->film_grain_params.num_cb_points; i++) {
    pic_param->film_grain_info.point_cb_value[i] =
        frame_header->film_grain_params.point_cb_value[i];
    pic_param->film_grain_info.point_cb_scaling[i] =
        frame_header->film_grain_params.point_cb_scaling[i];
  }

  pic_param->film_grain_info.num_cr_points =
      frame_header->film_grain_params.num_cr_points;
  for (i = 0; i < frame_header->film_grain_params.num_cr_points; i++) {
    pic_param->film_grain_info.point_cr_value[i] =
        frame_header->film_grain_params.point_cr_value[i];
    pic_param->film_grain_info.point_cr_scaling[i] =
        frame_header->film_grain_params.point_cr_scaling[i];
  }


  if (pic_param->film_grain_info.num_y_points) {
    for (i = 0; i < 24; i++) {
      pic_param->film_grain_info.ar_coeffs_y[i] =
          frame_header->film_grain_params.ar_coeffs_y_plus_128[i] - 128;
    }
  }
  if (frame_header->film_grain_params.chroma_scaling_from_luma
      || pic_param->film_grain_info.num_cb_points) {
    for (i = 0; i < GST_AV1_MAX_NUM_POS_LUMA; i++) {
      pic_param->film_grain_info.ar_coeffs_cb[i] =
          frame_header->film_grain_params.ar_coeffs_cb_plus_128[i] - 128;
    }
  }
  if (frame_header->film_grain_params.chroma_scaling_from_luma
      || pic_param->film_grain_info.num_cr_points) {
    for (i = 0; i < GST_AV1_MAX_NUM_POS_LUMA; i++) {
      pic_param->film_grain_info.ar_coeffs_cr[i] =
          frame_header->film_grain_params.ar_coeffs_cr_plus_128[i] - 128;
    }
  }
#define SUB_FIELD film_grain_info
  COPY_FILM_GRAIN_FIELD (cb_mult);
  COPY_FILM_GRAIN_FIELD (cb_luma_mult);
  COPY_FILM_GRAIN_FIELD (cb_offset);
  COPY_FILM_GRAIN_FIELD (cr_mult);
  COPY_FILM_GRAIN_FIELD (cr_luma_mult);
  COPY_FILM_GRAIN_FIELD (cr_offset);
#undef SUB_FIELD
#undef COPY_FILM_GRAIN_FIELD
}

static void
av1_fill_loop_filter_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header)
{
  guint i;

  pic_param->superres_scale_denominator = frame_header->superres_denom;
  pic_param->interp_filter = frame_header->interpolation_filter;
  pic_param->filter_level[0] =
      frame_header->loop_filter_params.loop_filter_level[0];
  pic_param->filter_level[1] =
      frame_header->loop_filter_params.loop_filter_level[1];
  pic_param->filter_level_u =
      frame_header->loop_filter_params.loop_filter_level[2];
  pic_param->filter_level_v =
      frame_header->loop_filter_params.loop_filter_level[3];
  pic_param->loop_filter_info_fields.bits.sharpness_level =
      frame_header->loop_filter_params.loop_filter_sharpness;
  pic_param->loop_filter_info_fields.bits.mode_ref_delta_enabled =
      frame_header->loop_filter_params.loop_filter_delta_enabled;
  pic_param->loop_filter_info_fields.bits.mode_ref_delta_update =
      frame_header->loop_filter_params.loop_filter_delta_update;

  for (i = 0; i < GST_AV1_TOTAL_REFS_PER_FRAME; i++)
    pic_param->ref_deltas[i] =
        frame_header->loop_filter_params.loop_filter_ref_deltas[i];
  for (i = 0; i < 2; i++)
    pic_param->mode_deltas[i] =
        frame_header->loop_filter_params.loop_filter_mode_deltas[i];

  pic_param->mode_control_fields.bits.delta_lf_present_flag =
      frame_header->loop_filter_params.delta_lf_present;
  pic_param->mode_control_fields.bits.log2_delta_lf_res =
      frame_header->loop_filter_params.delta_lf_res;
  pic_param->mode_control_fields.bits.delta_lf_multi =
      frame_header->loop_filter_params.delta_lf_multi;
}

static void
av1_fill_quantization_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header)
{
  pic_param->base_qindex = frame_header->quantization_params.base_q_idx;
  pic_param->y_dc_delta_q = frame_header->quantization_params.delta_q_y_dc;
  pic_param->u_dc_delta_q = frame_header->quantization_params.delta_q_u_dc;
  pic_param->u_ac_delta_q = frame_header->quantization_params.delta_q_u_ac;
  pic_param->v_dc_delta_q = frame_header->quantization_params.delta_q_v_dc;
  pic_param->v_ac_delta_q = frame_header->quantization_params.delta_q_v_ac;

  pic_param->qmatrix_fields.bits.using_qmatrix =
      frame_header->quantization_params.using_qmatrix;
  if (pic_param->qmatrix_fields.bits.using_qmatrix) {
    pic_param->qmatrix_fields.bits.qm_y =
        frame_header->quantization_params.qm_y;
    pic_param->qmatrix_fields.bits.qm_u =
        frame_header->quantization_params.qm_u;
    pic_param->qmatrix_fields.bits.qm_v =
        frame_header->quantization_params.qm_v;
  } else {
    pic_param->qmatrix_fields.bits.qm_y = 0;
    pic_param->qmatrix_fields.bits.qm_u = 0;
    pic_param->qmatrix_fields.bits.qm_v = 0;
  }

  pic_param->mode_control_fields.bits.delta_q_present_flag =
      frame_header->quantization_params.delta_q_present;
  pic_param->mode_control_fields.bits.log2_delta_q_res =
      frame_header->quantization_params.delta_q_res;
}

static void
av1_fill_cdef_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header, guint8 num_planes)
{
  guint8 sec_strength;
  guint i;

  pic_param->cdef_damping_minus_3 = frame_header->cdef_params.cdef_damping - 3;
  pic_param->cdef_bits = frame_header->cdef_params.cdef_bits;
  for (i = 0; i < GST_AV1_CDEF_MAX; i++) {
    sec_strength = frame_header->cdef_params.cdef_y_sec_strength[i];
    g_assert (sec_strength <= 4);
    /* may need to minus 1 in order to merge with primary value. */
    if (sec_strength == 4)
      sec_strength--;

    pic_param->cdef_y_strengths[i] =
        ((frame_header->cdef_params.cdef_y_pri_strength[i] & 0xf) << 2) |
        (sec_strength & 0x03);
  }
  if (num_planes > 1) {
    for (i = 0; i < GST_AV1_CDEF_MAX; i++) {
      sec_strength = frame_header->cdef_params.cdef_uv_sec_strength[i];
      g_assert (sec_strength <= 4);
      /* may need to minus 1 in order to merge with primary value. */
      if (sec_strength == 4)
        sec_strength--;

      pic_param->cdef_uv_strengths[i] =
          ((frame_header->cdef_params.cdef_uv_pri_strength[i] & 0xf) << 2) |
          (sec_strength & 0x03);
    }
  } else {
    for (i = 0; i < GST_AV1_CDEF_MAX; i++) {
      pic_param->cdef_uv_strengths[i] = 0;
    }
  }
}

static void
av1_fill_loop_restoration_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header)
{
  pic_param->loop_restoration_fields.bits.yframe_restoration_type =
      frame_header->loop_restoration_params.frame_restoration_type[0];
  pic_param->loop_restoration_fields.bits.cbframe_restoration_type =
      frame_header->loop_restoration_params.frame_restoration_type[1];
  pic_param->loop_restoration_fields.bits.crframe_restoration_type =
      frame_header->loop_restoration_params.frame_restoration_type[2];
  pic_param->loop_restoration_fields.bits.lr_unit_shift =
      frame_header->loop_restoration_params.lr_unit_shift;
  pic_param->loop_restoration_fields.bits.lr_uv_shift =
      frame_header->loop_restoration_params.lr_uv_shift;
}

static void
av1_fill_global_motion_info (VADecPictureParameterBufferAV1 * pic_param,
    GstAV1FrameHeaderOBU * frame_header)
{
  guint i, j;

  for (i = 0; i < 7; i++) {
    pic_param->wm[i].wmtype = (VAAV1TransformationType)
        frame_header->global_motion_params.gm_type[GST_AV1_REF_LAST_FRAME + i];

    for (j = 0; j < 6; j++)
      pic_param->wm[i].wmmat[j] =
          frame_header->global_motion_params.gm_params
          [GST_AV1_REF_LAST_FRAME + i][j];

    pic_param->wm[i].wmmat[6] = 0;
    pic_param->wm[i].wmmat[7] = 0;

    pic_param->wm[i].invalid =
        frame_header->global_motion_params.invalid[GST_AV1_REF_LAST_FRAME + i];
  }
}

static gboolean
av1_fill_picture_frame_header (GstVaapiDecoderAV1 * decoder,
    GstVaapiPictureAV1 * picture, GstAV1FrameHeaderOBU * frame_header)
{
  GstVaapiDecoderAV1Private *priv = &decoder->priv;
  VADecPictureParameterBufferAV1 *pic_param =
      GST_VAAPI_PICTURE (picture)->param;
  GstAV1SequenceHeaderOBU *seq_header = priv->seq_header;
  guint i;

  pic_param->profile = seq_header->seq_profile;
  pic_param->order_hint_bits_minus_1 = seq_header->order_hint_bits_minus_1;

  if (seq_header->bit_depth == 8)
    pic_param->bit_depth_idx = 0;
  else if (seq_header->bit_depth == 10)
    pic_param->bit_depth_idx = 1;
  else if (seq_header->bit_depth == 12)
    pic_param->bit_depth_idx = 2;
  else
    g_assert (0);

  pic_param->matrix_coefficients = seq_header->color_config.matrix_coefficients;

#define COPY_SEQ_FIELD(FP, FS) \
    pic_param->seq_info_fields.fields.FP = (seq_header)->FS

  COPY_SEQ_FIELD (still_picture, still_picture);
  COPY_SEQ_FIELD (use_128x128_superblock, use_128x128_superblock);
  COPY_SEQ_FIELD (enable_filter_intra, enable_filter_intra);
  COPY_SEQ_FIELD (enable_intra_edge_filter, enable_intra_edge_filter);
  COPY_SEQ_FIELD (enable_interintra_compound, enable_interintra_compound);
  COPY_SEQ_FIELD (enable_masked_compound, enable_masked_compound);
  COPY_SEQ_FIELD (enable_dual_filter, enable_dual_filter);
  COPY_SEQ_FIELD (enable_order_hint, enable_order_hint);
  COPY_SEQ_FIELD (enable_jnt_comp, enable_jnt_comp);
  COPY_SEQ_FIELD (enable_cdef, enable_cdef);
  COPY_SEQ_FIELD (mono_chrome, color_config.mono_chrome);
  COPY_SEQ_FIELD (color_range, color_config.color_range);
  COPY_SEQ_FIELD (subsampling_x, color_config.subsampling_x);
  COPY_SEQ_FIELD (subsampling_y, color_config.subsampling_y);
  COPY_SEQ_FIELD (film_grain_params_present, film_grain_params_present);
#undef COPY_SEQ_FIELD

  if (frame_header->film_grain_params.apply_grain) {
    g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE_ID (picture->recon_proxy) !=
        GST_VAAPI_PICTURE (picture)->surface_id);
    pic_param->current_frame =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (picture->recon_proxy);
    pic_param->current_display_picture =
        GST_VAAPI_PICTURE (picture)->surface_id;
  } else {
    pic_param->current_frame = GST_VAAPI_PICTURE (picture)->surface_id;
    pic_param->current_display_picture =
        GST_VAAPI_PICTURE (picture)->surface_id;
  }

  pic_param->frame_width_minus1 = frame_header->upscaled_width - 1;
  pic_param->frame_height_minus1 = frame_header->frame_height - 1;

  for (i = 0; i < GST_AV1_NUM_REF_FRAMES; i++) {
    if (priv->ref_frames[i])
      pic_param->ref_frame_map[i] =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (priv->ref_frames[i]->recon_proxy);
    else
      pic_param->ref_frame_map[i] = VA_INVALID_SURFACE;
  }
  for (i = 0; i < GST_AV1_REFS_PER_FRAME; i++) {
    pic_param->ref_frame_idx[i] = frame_header->ref_frame_idx[i];
  }
  pic_param->primary_ref_frame = frame_header->primary_ref_frame;
  pic_param->order_hint = frame_header->order_hint;

  av1_fill_segment_info (pic_param, frame_header);
  av1_fill_film_grain_info (pic_param, frame_header);

  pic_param->tile_cols = frame_header->tile_info.tile_cols;
  pic_param->tile_rows = frame_header->tile_info.tile_rows;
  for (i = 0; i < 63; i++) {
    pic_param->width_in_sbs_minus_1[i] =
        frame_header->tile_info.width_in_sbs_minus_1[i];
    pic_param->height_in_sbs_minus_1[i] =
        frame_header->tile_info.height_in_sbs_minus_1[i];
  }

  pic_param->context_update_tile_id =
      frame_header->tile_info.context_update_tile_id;

#define COPY_PIC_FIELD(FIELD) \
    pic_param->pic_info_fields.bits.FIELD = (frame_header)->FIELD

  COPY_PIC_FIELD (frame_type);
  COPY_PIC_FIELD (show_frame);
  COPY_PIC_FIELD (showable_frame);
  COPY_PIC_FIELD (error_resilient_mode);
  COPY_PIC_FIELD (disable_cdf_update);
  COPY_PIC_FIELD (allow_screen_content_tools);
  COPY_PIC_FIELD (force_integer_mv);
  COPY_PIC_FIELD (allow_intrabc);
  COPY_PIC_FIELD (use_superres);
  COPY_PIC_FIELD (allow_high_precision_mv);
  COPY_PIC_FIELD (is_motion_mode_switchable);
  COPY_PIC_FIELD (use_ref_frame_mvs);
  COPY_PIC_FIELD (disable_frame_end_update_cdf);
  pic_param->pic_info_fields.bits.uniform_tile_spacing_flag =
      frame_header->tile_info.uniform_tile_spacing_flag;
  COPY_PIC_FIELD (allow_warped_motion);
#undef COPY_PIC_FIELD

  av1_fill_loop_filter_info (pic_param, frame_header);
  av1_fill_quantization_info (pic_param, frame_header);

  pic_param->mode_control_fields.bits.tx_mode = frame_header->tx_mode;
  pic_param->mode_control_fields.bits.reference_select =
      frame_header->reference_select;
  pic_param->mode_control_fields.bits.reduced_tx_set_used =
      frame_header->reduced_tx_set;
  pic_param->mode_control_fields.bits.skip_mode_present =
      frame_header->skip_mode_present;

  av1_fill_cdef_info (pic_param, frame_header, seq_header->num_planes);
  av1_fill_loop_restoration_info (pic_param, frame_header);
  av1_fill_global_motion_info (pic_param, frame_header);

  return TRUE;
}

static GstVaapiDecoderStatus
av1_decode_frame_header (GstVaapiDecoderAV1 * decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderAV1Private *const priv = &decoder->priv;
  GstVaapiParserInfoAV1 *const pi = unit->parsed_info;
  GstAV1FrameHeaderOBU *frame_header = NULL;
  GstVaapiDecoderStatus ret = GST_VAAPI_DECODER_STATUS_SUCCESS;
  GstVaapiPictureAV1 *picture = NULL;

  if (pi->obu.obu_type == GST_AV1_OBU_FRAME_HEADER) {
    frame_header = &pi->frame_header;
  } else {
    g_assert (pi->obu.obu_type == GST_AV1_OBU_FRAME);
    frame_header = &pi->frame.frame_header;
  }

  if (frame_header->show_existing_frame) {
    GstVaapiPictureAV1 *to_show_picture = NULL;

    to_show_picture = priv->ref_frames[frame_header->frame_to_show_map_idx];
    if (to_show_picture == NULL) {
      GST_ERROR ("frame_to_show_map_idx point to a invalid picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    picture = (GstVaapiPictureAV1 *)
        gst_vaapi_picture_new_clone (GST_VAAPI_PICTURE_CAST (to_show_picture));
    if (!picture)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE;
    gst_vaapi_surface_proxy_replace (&picture->recon_proxy,
        to_show_picture->recon_proxy);
    picture->cloned = TRUE;
    GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);

    picture->frame_header = to_show_picture->frame_header;
  } else {
    /* Resolution changed */
    if (priv->width != priv->seq_header->max_frame_width_minus_1 + 1 ||
        priv->height != priv->seq_header->max_frame_height_minus_1 + 1) {
      priv->reset_context = TRUE;
      priv->width = priv->seq_header->max_frame_width_minus_1 + 1;
      priv->height = priv->seq_header->max_frame_height_minus_1 + 1;
      GST_INFO ("change the resolution to %dx%d", priv->width, priv->height);
    }

    ret = av1_decoder_ensure_context (decoder);
    if (ret != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return ret;

    picture = gst_vaapi_picture_av1_new (decoder);
    if (!picture)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE;

    if (frame_header->upscaled_width != priv->width ||
        frame_header->frame_height != priv->height) {
      GstVaapiRectangle crop_rect;

      if (frame_header->upscaled_width > priv->width) {
        GST_WARNING ("Frame width is %d, bigger than sequence max width %d",
            frame_header->upscaled_width, priv->width);
        gst_vaapi_codec_object_unref (picture);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      }
      if (frame_header->frame_height > priv->height) {
        GST_WARNING ("Frame height is %d, bigger than sequence max height %d",
            frame_header->frame_height, priv->height);
        gst_vaapi_codec_object_unref (picture);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      }

      crop_rect.x = 0;
      crop_rect.y = 0;
      crop_rect.width = frame_header->upscaled_width;
      crop_rect.height = frame_header->frame_height;
      gst_vaapi_picture_set_crop_rect (GST_VAAPI_PICTURE (picture), &crop_rect);
    }

    if (frame_header->film_grain_params.apply_grain) {
      GstVaapiSurfaceProxy *recon_proxy = gst_vaapi_context_get_surface_proxy
          (GST_VAAPI_DECODER (decoder)->context);
      if (!recon_proxy) {
        gst_vaapi_codec_object_unref (picture);
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE;
      }
      gst_vaapi_surface_proxy_replace (&picture->recon_proxy, recon_proxy);
    }

    picture->frame_header = *frame_header;

    /* this frame will not show this time */
    if (!frame_header->show_frame)
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);

    GST_VAAPI_PICTURE (picture)->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    GST_VAAPI_PICTURE (picture)->type =
        frame_header->frame_is_intra ? GST_VAAPI_PICTURE_TYPE_I :
        GST_VAAPI_PICTURE_TYPE_P;

    if (!av1_fill_picture_frame_header (decoder, picture, frame_header)) {
      gst_vaapi_codec_object_unref (picture);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }
  }

  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  return ret;
}

static GstVaapiDecoderStatus
av1_decode_tile_data (GstVaapiDecoderAV1 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderAV1Private *const priv = &decoder->priv;
  GstVaapiPictureAV1 *picture = priv->current_picture;
  GstVaapiParserInfoAV1 *const pi = unit->parsed_info;
  GstAV1TileGroupOBU *tile_group = &pi->tile_group;
  guint32 i;
  GstVaapiSlice *slice;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  if (!picture) {
    GST_WARNING ("Decode the tile date without a picture");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  slice = GST_VAAPI_SLICE_NEW_N_PARAMS (AV1, decoder,
      map_info.data + pi->data_offset + unit->offset, pi->obu.obu_size,
      (tile_group->tg_end - tile_group->tg_start + 1));
  gst_buffer_unmap (buffer, &map_info);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  for (i = 0; i < tile_group->tg_end - tile_group->tg_start + 1; i++) {
    VASliceParameterBufferAV1 *const slice_param =
        slice->param + i * sizeof (VASliceParameterBufferAV1);

    slice_param->slice_data_size =
        tile_group->entry[tile_group->tg_start + i].tile_size;
    slice_param->slice_data_offset =
        tile_group->entry[tile_group->tg_start + i].tile_offset;
    slice_param->tile_row =
        tile_group->entry[tile_group->tg_start + i].tile_row;
    slice_param->tile_column =
        tile_group->entry[tile_group->tg_start + i].tile_col;
    slice_param->slice_data_flag = 0;
  }

  gst_vaapi_picture_add_slice (GST_VAAPI_PICTURE_CAST (picture), slice);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
av1_decode_unit (GstVaapiDecoderAV1 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderStatus ret = GST_VAAPI_DECODER_STATUS_SUCCESS;
  GstVaapiParserInfoAV1 *const pi = unit->parsed_info;

  GST_DEBUG ("begin to decode the unit of %s", av1_obu_name (pi->obu.obu_type));

  switch (pi->obu.obu_type) {
    case GST_AV1_OBU_SEQUENCE_HEADER:
      ret = av1_decode_seqeunce (decoder, unit);
      break;
    case GST_AV1_OBU_FRAME_HEADER:
      ret = av1_decode_frame_header (decoder, unit);
      break;
    case GST_AV1_OBU_FRAME:
      ret = av1_decode_frame_header (decoder, unit);
      if (ret != GST_VAAPI_DECODER_STATUS_SUCCESS)
        break;
      /* fall through */
    case GST_AV1_OBU_TILE_GROUP:
      ret = av1_decode_tile_data (decoder, unit);
      break;
    case GST_AV1_OBU_METADATA:
    case GST_AV1_OBU_REDUNDANT_FRAME_HEADER:
    case GST_AV1_OBU_PADDING:
      /* Not handled */
      ret = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    default:
      GST_WARNING ("can not handle obu type %s",
          av1_obu_name (pi->obu.obu_type));
      ret = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  return ret;
}

static GstVaapiDecoderStatus
av1_decode_current_picture (GstVaapiDecoderAV1 * decoder)
{
  GstVaapiDecoderAV1Private *priv = &decoder->priv;
  GstVaapiPictureAV1 *const picture =
      (GstVaapiPictureAV1 *) priv->current_picture;

  g_assert (picture);

  if (!gst_vaapi_picture_decode_with_surface_id (GST_VAAPI_PICTURE (picture),
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (picture->recon_proxy)))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
av1_decoder_update_state (GstVaapiDecoderAV1 * decoder,
    GstVaapiPictureAV1 * picture)
{
  GstVaapiDecoderAV1Private *priv = &decoder->priv;
  GstAV1ParserResult ret;
  guint i;

  /* This is a show_existing_frame case, only update key frame */
  if (picture->cloned && picture->frame_header.frame_type != GST_AV1_KEY_FRAME)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  ret = gst_av1_parser_reference_frame_update (priv->parser,
      &picture->frame_header);
  if (ret != GST_AV1_PARSER_OK) {
    GST_ERROR ("failed to update the reference.");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  for (i = 0; i < GST_AV1_NUM_REF_FRAMES; i++) {
    if ((picture->frame_header.refresh_frame_flags >> i) & 1) {
      GST_LOG ("reference frame %p to ref slot:%d", picture, i);
      gst_vaapi_picture_replace (&priv->ref_frames[i], picture);
    }
  }

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
av1_decoder_reset (GstVaapiDecoderAV1 * decoder)
{
  GstVaapiDecoderAV1Private *priv = &decoder->priv;
  guint i;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->width = 0;
  priv->height = 0;
  priv->annex_b = FALSE;
  priv->reset_context = FALSE;

  if (priv->current_picture)
    gst_vaapi_picture_replace (&priv->current_picture, NULL);

  if (priv->seq_header) {
    g_free (priv->seq_header);
    priv->seq_header = NULL;
  }

  for (i = 0; i < GST_AV1_NUM_REF_FRAMES; i++)
    gst_vaapi_picture_replace (&priv->ref_frames[i], NULL);
}

static gboolean
av1_is_picture_end (GstVaapiParserInfoAV1 * pi)
{
  GstAV1TileGroupOBU *tile_group = NULL;

  if (pi->obu.obu_type == GST_AV1_OBU_FRAME) {
    tile_group = &pi->frame.tile_group;
  } else if (pi->obu.obu_type == GST_AV1_OBU_TILE_GROUP) {
    tile_group = &pi->tile_group;
  }
  g_assert (tile_group);

  if (tile_group->tg_end == tile_group->num_tiles - 1)
    return TRUE;

  return FALSE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_av1_reset (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderAV1 *const decoder = GST_VAAPI_DECODER_AV1 (base_decoder);
  GstVaapiDecoderAV1Private *priv = &decoder->priv;

  av1_decoder_reset (decoder);
  gst_av1_parser_reset (priv->parser, FALSE);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_av1_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderAV1 *const decoder = GST_VAAPI_DECODER_AV1 (base_decoder);
  GstVaapiDecoderAV1Private *const priv = &decoder->priv;
  GstVaapiParserInfoAV1 *pi;
  GstAV1Parser *parser = priv->parser;
  guchar *buf;
  guint buf_size, flags;
  GstAV1OBU obu;
  GstAV1ParserResult av1_ret;
  GstVaapiDecoderStatus ret = GST_VAAPI_DECODER_STATUS_SUCCESS;
  guint32 consumed;

  buf_size = gst_adapter_available (adapter);
  if (!buf_size)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  /* no need to do unmap here */
  buf = (guchar *) gst_adapter_map (adapter, buf_size);
  if (!buf)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  av1_ret =
      gst_av1_parser_identify_one_obu (parser, buf, buf_size, &obu, &consumed);
  if (av1_ret == GST_AV1_PARSER_DROP) {
    GST_LOG ("just discard a %s obu with size %d, consumed %d",
        av1_obu_name (obu.obu_type), obu.obu_size, consumed);
    gst_adapter_flush (adapter, consumed);
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  } else if (av1_ret == GST_AV1_PARSER_NO_MORE_DATA) {
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
  } else if (av1_ret == GST_AV1_PARSER_BITSTREAM_ERROR) {
    GST_WARNING_OBJECT (decoder, "parse error, an invalid bitstream");
    gst_adapter_flush (adapter, consumed);
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  } else if (av1_ret != GST_AV1_PARSER_OK) {
    GST_WARNING_OBJECT (decoder, "parse error, unknown error");
    gst_adapter_flush (adapter, consumed);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  GST_DEBUG ("get one %s obu with size %d, consumed %d",
      av1_obu_name (obu.obu_type), obu.obu_size, consumed);

  pi = parser_info_av1_new (&obu);
  if (!pi) {
    gst_adapter_flush (adapter, consumed);
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, pi,
      (GDestroyNotify) gst_vaapi_mini_object_unref);

  flags = 0;
  av1_ret = GST_AV1_PARSER_OK;
  switch (pi->obu.obu_type) {
    case GST_AV1_OBU_TEMPORAL_DELIMITER:
      av1_ret = gst_av1_parser_parse_temporal_delimiter_obu (parser, &pi->obu);
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
    case GST_AV1_OBU_SEQUENCE_HEADER:
      av1_ret = gst_av1_parser_parse_sequence_header_obu (parser, &pi->obu,
          &pi->seq_header);
      break;
    case GST_AV1_OBU_REDUNDANT_FRAME_HEADER:
      av1_ret = gst_av1_parser_parse_frame_header_obu (parser, &pi->obu,
          &pi->frame_header);
      break;
    case GST_AV1_OBU_FRAME_HEADER:
      av1_ret = gst_av1_parser_parse_frame_header_obu (parser, &pi->obu,
          &pi->frame_header);
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      if (pi->frame_header.show_existing_frame) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      }
      break;
    case GST_AV1_OBU_FRAME:
      av1_ret = gst_av1_parser_parse_frame_obu (parser, &obu, &pi->frame);
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      pi->data_offset = obu.data - buf;
      if (av1_is_picture_end (pi))
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      break;
    case GST_AV1_OBU_METADATA:
      av1_ret = gst_av1_parser_parse_metadata_obu (parser, &obu, &pi->metadata);
      break;
    case GST_AV1_OBU_TILE_GROUP:
      av1_ret = gst_av1_parser_parse_tile_group_obu (parser, &obu,
          &pi->tile_group);
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      pi->data_offset = obu.data - buf;
      if (av1_is_picture_end (pi))
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      break;
    case GST_AV1_OBU_TILE_LIST:
      av1_ret =
          gst_av1_parser_parse_tile_list_obu (parser, &obu, &pi->tile_list);
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      break;
    case GST_AV1_OBU_PADDING:
      break;
    default:
      GST_WARNING_OBJECT (decoder, "an unrecognized obu type %d", obu.obu_type);
      av1_ret = GST_AV1_PARSER_BITSTREAM_ERROR;
      break;
  }

  if (av1_ret != GST_AV1_PARSER_OK) {
    /* Should not get NO_MORE_DATA, the obu size is already known */
    GST_WARNING_OBJECT (decoder, "parse %s obu error",
        av1_obu_name (pi->obu.obu_type));
    gst_adapter_flush (adapter, consumed);
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (pi));
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  unit->size = consumed;
  unit->offset = pi->obu.data - buf;
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);

  return ret;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_av1_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderAV1 *const decoder = GST_VAAPI_DECODER_AV1 (base_decoder);
  GstVaapiDecoderStatus status;

  status = av1_decode_unit (decoder, unit);

  return status;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_av1_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * base_unit)
{
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_av1_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderAV1 *const decoder = GST_VAAPI_DECODER_AV1 (base_decoder);
  GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;
  GstVaapiDecoderAV1Private *priv = &decoder->priv;

  if (!priv->current_picture->cloned)
    status = av1_decode_current_picture (decoder);

  /* update state anyway */
  av1_decoder_update_state (decoder, priv->current_picture);

  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    goto out;

  if (!gst_vaapi_picture_output (GST_VAAPI_PICTURE (priv->current_picture)))
    status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

out:
  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  return status;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_av1_flush (GstVaapiDecoder * base_decoder)
{
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_av1_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);
  GstVaapiDecoderAV1 *const decoder = GST_VAAPI_DECODER_AV1 (base_decoder);
  GstVaapiDecoderAV1Private *priv = &decoder->priv;

  av1_decoder_reset (decoder);
  if (decoder->priv.parser)
    gst_av1_parser_free (decoder->priv.parser);
  priv->parser = NULL;

  G_OBJECT_CLASS (gst_vaapi_decoder_av1_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_av1_class_init (GstVaapiDecoderAV1Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_av1_finalize;

  decoder_class->reset = gst_vaapi_decoder_av1_reset;
  decoder_class->parse = gst_vaapi_decoder_av1_parse;
  decoder_class->decode = gst_vaapi_decoder_av1_decode;
  decoder_class->start_frame = gst_vaapi_decoder_av1_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_av1_end_frame;
  decoder_class->flush = gst_vaapi_decoder_av1_flush;
}

static void
gst_vaapi_decoder_av1_init (GstVaapiDecoderAV1 * decoder)
{
  guint i;
  GstVaapiDecoderAV1Private *priv = &decoder->priv;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->width = 0;
  priv->height = 0;
  priv->annex_b = FALSE;
  priv->reset_context = FALSE;
  priv->current_picture = NULL;
  priv->seq_header = NULL;

  for (i = 0; i < GST_AV1_NUM_REF_FRAMES; i++)
    priv->ref_frames[i] = NULL;

  priv->parser = gst_av1_parser_new ();
}

/**
 * gst_vaapi_decoder_av1_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for AV1 decoding. The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_av1_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_AV1, "display", display,
      "caps", caps, NULL);
}
