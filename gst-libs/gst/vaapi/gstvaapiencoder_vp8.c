/*
 *  gstvaapiencoder_vp8.c - VP8 encoder
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "sysdeps.h"
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gstvp8parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapiencoder_vp8.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                  \
  (GST_VAAPI_RATECONTROL_MASK (CQP) |           \
   GST_VAAPI_RATECONTROL_MASK (CBR) |           \
   GST_VAAPI_RATECONTROL_MASK (VBR))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_NONE)

#define DEFAULT_LOOP_FILTER_LEVEL 0
#define DEFAULT_SHARPNESS_LEVEL 0
#define DEFAULT_YAC_QI 40

/* ------------------------------------------------------------------------- */
/* --- VP8 Encoder                                                      --- */
/* ------------------------------------------------------------------------- */

struct _GstVaapiEncoderVP8
{
  GstVaapiEncoder parent_instance;
  GstVaapiProfile profile;
  guint loop_filter_level;
  guint sharpness_level;
  guint yac_qi;
  guint frame_num;
  /* reference list */
  GstVaapiSurfaceProxy *last_ref;
  GstVaapiSurfaceProxy *golden_ref;
  GstVaapiSurfaceProxy *alt_ref;
};

/* Derives the profile that suits best to the configuration */
static GstVaapiEncoderStatus
ensure_profile (GstVaapiEncoderVP8 * encoder)
{
  /* Always start from "simple" profile for maximum compatibility */
  encoder->profile = GST_VAAPI_PROFILE_VP8;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/* Derives the profile supported by the underlying hardware */
static gboolean
ensure_hw_profile (GstVaapiEncoderVP8 * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  GstVaapiProfile profile, profiles[2];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = encoder->profile;

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  for (i = 0; i < num_profiles; i++) {
    if (gst_vaapi_display_has_encoder (display, profiles[i], entrypoint)) {
      profile = profiles[i];
      break;
    }
  }
  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    goto error_unsupported_profile;

  GST_VAAPI_ENCODER_CAST (encoder)->profile = profile;
  return TRUE;

  /* ERRORS */
error_unsupported_profile:
  {
    GST_ERROR ("unsupported HW profile %s",
        gst_vaapi_profile_get_va_name (encoder->profile));
    return FALSE;
  }
}

static gboolean
ensure_bitrate (GstVaapiEncoderVP8 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  /* Default compression: 64 bits per macroblock  */
  switch (GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    case GST_VAAPI_RATECONTROL_CBR:
    case GST_VAAPI_RATECONTROL_VBR:
      if (!base_encoder->bitrate) {
        base_encoder->bitrate =
            gst_util_uint64_scale (GST_VAAPI_ENCODER_WIDTH (encoder) *
            GST_VAAPI_ENCODER_HEIGHT (encoder),
            GST_VAAPI_ENCODER_FPS_N (encoder),
            GST_VAAPI_ENCODER_FPS_D (encoder)) / (4 * 1000);
      }
      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }

  return TRUE;
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP8 *encoder = GST_VAAPI_ENCODER_VP8 (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);

  /* Maximum sizes for common headers (in bytes) */
  enum
  {
    MAX_FRAME_TAG_SIZE = 10,
    MAX_UPDATE_SEGMENTATION_SIZE = 13,
    MAX_MB_LF_ADJUSTMENTS_SIZE = 9,
    MAX_QUANT_INDICES_SIZE = 5,
    MAX_TOKEN_PROB_UPDATE_SIZE = 1188,
    MAX_MV_PROBE_UPDATE_SIZE = 38,
    MAX_REST_OF_FRAME_HDR_SIZE = 15
  };

  if (!ensure_hw_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames = 3;

  /* Only YUV 4:2:0 formats are supported for now. */
  /* Assumig 4 times compression ratio */
  base_encoder->codedbuf_size = GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) * 12 / 4;

  base_encoder->codedbuf_size +=
      MAX_FRAME_TAG_SIZE + MAX_UPDATE_SEGMENTATION_SIZE +
      MAX_MB_LF_ADJUSTMENTS_SIZE + MAX_QUANT_INDICES_SIZE +
      MAX_TOKEN_PROB_UPDATE_SIZE + MAX_MV_PROBE_UPDATE_SIZE +
      MAX_REST_OF_FRAME_HDR_SIZE;

  base_encoder->context_info.profile = base_encoder->profile;
  base_encoder->context_info.entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static void
clear_ref (GstVaapiEncoderVP8 * encoder, GstVaapiSurfaceProxy ** ref)
{
  if (*ref) {
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder), *ref);
    *ref = NULL;
  }
}

static void
clear_references (GstVaapiEncoderVP8 * encoder)
{
  clear_ref (encoder, &encoder->last_ref);
  clear_ref (encoder, &encoder->golden_ref);
  clear_ref (encoder, &encoder->alt_ref);
}

static void
push_reference (GstVaapiEncoderVP8 * encoder, GstVaapiSurfaceProxy * ref)
{
  if (encoder->last_ref == NULL) {
    encoder->golden_ref = gst_vaapi_surface_proxy_ref (ref);
    encoder->alt_ref = gst_vaapi_surface_proxy_ref (ref);
  } else {
    clear_ref (encoder, &encoder->alt_ref);
    encoder->alt_ref = encoder->golden_ref;
    encoder->golden_ref = encoder->last_ref;
  }
  encoder->last_ref = ref;
}

static gboolean
fill_sequence (GstVaapiEncoderVP8 * encoder, GstVaapiEncSequence * sequence)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  VAEncSequenceParameterBufferVP8 *const seq_param = sequence->param;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferVP8));

  seq_param->frame_width = GST_VAAPI_ENCODER_WIDTH (encoder);
  seq_param->frame_height = GST_VAAPI_ENCODER_HEIGHT (encoder);

  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CBR ||
      GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_VBR)
    seq_param->bits_per_second = base_encoder->bitrate * 1000;

  seq_param->intra_period = base_encoder->keyframe_period;

  return TRUE;
}

static gboolean
ensure_sequence (GstVaapiEncoderVP8 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (VP8, encoder);
  if (!sequence)
    goto error;

  if (!fill_sequence (encoder, sequence))
    goto error;

  gst_vaapi_enc_picture_set_sequence (picture, sequence);
  gst_vaapi_codec_object_replace (&sequence, NULL);
  return TRUE;

  /* ERRORS */
error:
  {
    gst_vaapi_codec_object_replace (&sequence, NULL);
    return FALSE;
  }
}

static gboolean
ensure_control_rate_params (GstVaapiEncoderVP8 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CQP)
    return TRUE;

  /* RateControl params */
  GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder).initial_qp = encoder->yac_qi;
  GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder).min_qp = 1;

  /* *INDENT-OFF* */
  /* HRD params */
  GST_VAAPI_ENCODER_VA_HRD (encoder) = (VAEncMiscParameterHRD) {
    .buffer_size = base_encoder->bitrate * 1000 * 2,
    .initial_buffer_fullness = base_encoder->bitrate * 1000,
  };
  /* *INDENT-ON* */

  return TRUE;
}

static gboolean
ensure_misc_params (GstVaapiEncoderVP8 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  if (!gst_vaapi_encoder_ensure_param_quality_level (base_encoder, picture))
    return FALSE;

  if (!gst_vaapi_encoder_ensure_param_control_rate (base_encoder, picture))
    return FALSE;

  return TRUE;
}

static gboolean
fill_picture (GstVaapiEncoderVP8 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferVP8 *const pic_param = picture->param;
  int i;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferVP8));

  pic_param->reconstructed_frame = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->coded_buf = GST_VAAPI_CODED_BUFFER_ID (codedbuf);

  if (picture->type == GST_VAAPI_PICTURE_TYPE_P) {
    pic_param->pic_flags.bits.frame_type = 1;
    pic_param->ref_arf_frame =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->alt_ref);
    pic_param->ref_gf_frame =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->golden_ref);
    pic_param->ref_last_frame =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->last_ref);
    pic_param->pic_flags.bits.refresh_last = 1;
    pic_param->pic_flags.bits.refresh_golden_frame = 0;
    pic_param->pic_flags.bits.copy_buffer_to_golden = 1;
    pic_param->pic_flags.bits.refresh_alternate_frame = 0;
    pic_param->pic_flags.bits.copy_buffer_to_alternate = 2;
  } else {
    pic_param->ref_last_frame = VA_INVALID_SURFACE;
    pic_param->ref_gf_frame = VA_INVALID_SURFACE;
    pic_param->ref_arf_frame = VA_INVALID_SURFACE;
    pic_param->pic_flags.bits.refresh_last = 1;
    pic_param->pic_flags.bits.refresh_golden_frame = 1;
    pic_param->pic_flags.bits.refresh_alternate_frame = 1;
  }

  pic_param->pic_flags.bits.show_frame = 1;

  if (encoder->loop_filter_level) {
    pic_param->pic_flags.bits.version = 1;
    pic_param->pic_flags.bits.loop_filter_type = 1;     /* Enable simple loop filter */
    /* Disabled segmentation, so what matters is only loop_filter_level[0] */
    for (i = 0; i < 4; i++)
      pic_param->loop_filter_level[i] = encoder->loop_filter_level;
  }

  pic_param->sharpness_level = encoder->sharpness_level;

  /* Used for CBR */
  pic_param->clamp_qindex_low = 0;
  pic_param->clamp_qindex_high = 127;

  return TRUE;
}

static gboolean
ensure_picture (GstVaapiEncoderVP8 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_picture (encoder, picture, codedbuf, surface))
    return FALSE;

  return TRUE;
}

static gboolean
fill_quantization_table (GstVaapiEncoderVP8 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncQMatrix * q_matrix)
{
  VAQMatrixBufferVP8 *const qmatrix_param = q_matrix->param;
  int i;

  memset (qmatrix_param, 0, sizeof (VAQMatrixBufferVP8));

  /* DefaultYacQantVal = 8 for I frame, which is ac_qlookup[4] and
   * DefaultYacQantVAl = 44 for P frame, which is ac_qllookup[40] */
  for (i = 0; i < 4; i++) {
    if (encoder->yac_qi == DEFAULT_YAC_QI) {
      if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
        qmatrix_param->quantization_index[i] = 4;
      else
        qmatrix_param->quantization_index[i] = 40;
    } else
      qmatrix_param->quantization_index[i] = encoder->yac_qi;
  }

  return TRUE;
}

static gboolean
ensure_quantization_table (GstVaapiEncoderVP8 * encoder,
    GstVaapiEncPicture * picture)
{
  g_assert (picture);

  picture->q_matrix = GST_VAAPI_ENC_Q_MATRIX_NEW (VP8, encoder);
  if (!picture->q_matrix) {
    GST_ERROR ("failed to allocate quantiser table");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (!fill_quantization_table (encoder, picture, picture->q_matrix))
    return FALSE;

  return TRUE;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp8_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base_encoder);

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));

  if (!ensure_sequence (encoder, picture))
    goto error;
  if (!ensure_misc_params (encoder, picture))
    goto error;
  if (!ensure_picture (encoder, picture, codedbuf, reconstruct))
    goto error;
  if (!ensure_quantization_table (encoder, picture))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;
  if (reconstruct) {
    if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
      clear_references (encoder);
    push_reference (encoder, reconstruct);
  }

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    if (reconstruct)
      gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
          reconstruct);
    return ret;
  }
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp8_flush (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (base_encoder);

  encoder->frame_num = 0;
  clear_references (encoder);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp8_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (base_encoder);
  GstVaapiEncPicture *picture = NULL;
  GstVaapiEncoderStatus status = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  if (!frame)
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

  picture = GST_VAAPI_ENC_PICTURE_NEW (VP8, encoder, frame);
  if (!picture) {
    GST_WARNING ("create VP8 picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (encoder->frame_num >= base_encoder->keyframe_period) {
    encoder->frame_num = 0;
    clear_references (encoder);
  }
  if (encoder->frame_num == 0) {
    picture->type = GST_VAAPI_PICTURE_TYPE_I;
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  } else {
    picture->type = GST_VAAPI_PICTURE_TYPE_P;
  }

  encoder->frame_num++;
  *output = picture;
  return status;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp8_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (base_encoder);
  GstVaapiEncoderStatus status;

  status = ensure_profile (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  if (!ensure_bitrate (encoder))
    goto error;

  ensure_control_rate_params (encoder);
  return set_context_info (base_encoder);

  /* ERRORS */
error:
  {
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

struct _GstVaapiEncoderVP8Class
{
  GstVaapiEncoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiEncoderVP8, gst_vaapi_encoder_vp8,
    GST_TYPE_VAAPI_ENCODER);

static void
gst_vaapi_encoder_vp8_init (GstVaapiEncoderVP8 * encoder)
{
  encoder->frame_num = 0;
  encoder->last_ref = NULL;
  encoder->golden_ref = NULL;
  encoder->alt_ref = NULL;
}

static void
gst_vaapi_encoder_vp8_finalize (GObject * object)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (object);
  clear_references (encoder);
  G_OBJECT_CLASS (gst_vaapi_encoder_vp8_parent_class)->finalize (object);
}

/**
 * @ENCODER_VP8_PROP_RATECONTROL: Rate control (#GstVaapiRateControl).
 * @ENCODER_VP8_PROP_TUNE: The tuning options (#GstVaapiEncoderTune).
 * @ENCODER_VP8_PROP_LOOP_FILTER_LEVEL: Loop Filter Level(uint).
 * @ENCODER_VP8_PROP_LOOP_SHARPNESS_LEVEL: Sharpness Level(uint).
 * @ENCODER_VP8_PROP_YAC_Q_INDEX: Quantization table index for luma AC(uint).
 *
 * The set of VP8 encoder specific configurable properties.
 */
enum
{
  ENCODER_VP8_PROP_RATECONTROL = 1,
  ENCODER_VP8_PROP_TUNE,
  ENCODER_VP8_PROP_LOOP_FILTER_LEVEL,
  ENCODER_VP8_PROP_SHARPNESS_LEVEL,
  ENCODER_VP8_PROP_YAC_Q_INDEX,
  ENCODER_VP8_N_PROPERTIES
};

static GParamSpec *properties[ENCODER_VP8_N_PROPERTIES];

static void
gst_vaapi_encoder_vp8_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (object);

  if (base_encoder->num_codedbuf_queued > 0) {
    GST_ERROR_OBJECT (object,
        "failed to set any property after encoding started");
    return;
  }

  switch (prop_id) {
    case ENCODER_VP8_PROP_RATECONTROL:
      gst_vaapi_encoder_set_rate_control (base_encoder,
          g_value_get_enum (value));
      break;
    case ENCODER_VP8_PROP_TUNE:
      gst_vaapi_encoder_set_tuning (base_encoder, g_value_get_enum (value));
      break;
    case ENCODER_VP8_PROP_LOOP_FILTER_LEVEL:
      encoder->loop_filter_level = g_value_get_uint (value);
      break;
    case ENCODER_VP8_PROP_SHARPNESS_LEVEL:
      encoder->sharpness_level = g_value_get_uint (value);
      break;
    case ENCODER_VP8_PROP_YAC_Q_INDEX:
      encoder->yac_qi = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_vaapi_encoder_vp8_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8 (object);
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);

  switch (prop_id) {
    case ENCODER_VP8_PROP_RATECONTROL:
      g_value_set_enum (value, base_encoder->rate_control);
      break;
    case ENCODER_VP8_PROP_TUNE:
      g_value_set_enum (value, base_encoder->tune);
      break;
    case ENCODER_VP8_PROP_LOOP_FILTER_LEVEL:
      g_value_set_uint (value, encoder->loop_filter_level);
      break;
    case ENCODER_VP8_PROP_SHARPNESS_LEVEL:
      g_value_set_uint (value, encoder->sharpness_level);
      break;
    case ENCODER_VP8_PROP_YAC_Q_INDEX:
      g_value_set_uint (value, encoder->yac_qi);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (VP8);

static void
gst_vaapi_encoder_vp8_class_init (GstVaapiEncoderVP8Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiEncoderClass *const encoder_class = GST_VAAPI_ENCODER_CLASS (klass);

  encoder_class->class_data = &g_class_data;
  encoder_class->reconfigure = gst_vaapi_encoder_vp8_reconfigure;
  encoder_class->reordering = gst_vaapi_encoder_vp8_reordering;
  encoder_class->encode = gst_vaapi_encoder_vp8_encode;
  encoder_class->flush = gst_vaapi_encoder_vp8_flush;

  object_class->set_property = gst_vaapi_encoder_vp8_set_property;
  object_class->get_property = gst_vaapi_encoder_vp8_get_property;
  object_class->finalize = gst_vaapi_encoder_vp8_finalize;

  properties[ENCODER_VP8_PROP_RATECONTROL] =
      g_param_spec_enum ("rate-control",
      "Rate Control", "Rate control mode",
      g_class_data.rate_control_get_type (),
      g_class_data.default_rate_control,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP8_PROP_TUNE] =
      g_param_spec_enum ("tune", "Encoder Tuning", "Encoder tuning option",
      g_class_data.encoder_tune_get_type (),
      g_class_data.default_encoder_tune,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP8_PROP_LOOP_FILTER_LEVEL] =
      g_param_spec_uint ("loop-filter-level", "Loop Filter Level",
      "Controls the deblocking filter strength", 0, 63,
      DEFAULT_LOOP_FILTER_LEVEL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP8_PROP_SHARPNESS_LEVEL] =
      g_param_spec_uint ("sharpness-level", "Sharpness Level",
      "Controls the deblocking filter sensitivity", 0, 7,
      DEFAULT_SHARPNESS_LEVEL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP8_PROP_YAC_Q_INDEX] =
      g_param_spec_uint ("yac-qi",
      "Luma AC Quant Table index",
      "Quantization Table index for Luma AC Coefficients,"
      " (in default case, yac_qi=4 for key frames and yac_qi=40"
      " for P frames)",
      0, 127, DEFAULT_YAC_QI,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  g_object_class_install_properties (object_class, ENCODER_VP8_N_PROPERTIES,
      properties);

  gst_type_mark_as_plugin_api (g_class_data.rate_control_get_type (), 0);
  gst_type_mark_as_plugin_api (g_class_data.encoder_tune_get_type (), 0);
}

/**
 * gst_vaapi_encoder_vp8_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for VP8 encoding.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_vp8_new (GstVaapiDisplay * display)
{
  return g_object_new (GST_TYPE_VAAPI_ENCODER_VP8, "display", display, NULL);
}
