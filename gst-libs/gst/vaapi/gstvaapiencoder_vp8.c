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
#include <va/va.h>
#include <va/va_enc_vp8.h>
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
   GST_VAAPI_RATECONTROL_MASK (CBR))

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

#define GST_VAAPI_ENCODER_VP8_CAST(encoder) \
    ((GstVaapiEncoderVP8 *)(encoder))

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
    GST_ERROR ("unsupported HW profile (0x%08x)", encoder->profile);
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
  GstVaapiEncoderVP8 *encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);
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

  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) & GST_VAAPI_RATECONTROL_CBR)
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
ensure_misc_params (GstVaapiEncoderVP8 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  GstVaapiEncMiscParam *misc;

  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) != GST_VAAPI_RATECONTROL_CBR)
    return TRUE;

  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (FrameRate, encoder);
  if (!misc)
    return FALSE;
  {
    VAEncMiscParameterFrameRate *param = misc->data;
    param->framerate =
        GST_VAAPI_ENCODER_FPS_N (encoder) / GST_VAAPI_ENCODER_FPS_D (encoder);
  }
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, encoder);
  if (!misc)
    return FALSE;
  {
    VAEncMiscParameterHRD *hrd = misc->data;
    if (base_encoder->bitrate > 0) {
      hrd->buffer_size = base_encoder->bitrate * 1000 * 2;
      hrd->initial_buffer_fullness = base_encoder->bitrate * 1000;
    } else {
      hrd->buffer_size = 0;
      hrd->initial_buffer_fullness = 0;
    }
  }

  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  /* RateControl params */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (RateControl, encoder);
  if (!misc)
    return FALSE;
  {
    VAEncMiscParameterRateControl *rate_control;
    rate_control = misc->data;
    rate_control->bits_per_second = base_encoder->bitrate * 1000;
    rate_control->target_percentage = 70;
    /* CPB (Coded picture buffer) length in milliseconds, which could
     * be provided as a property */
    rate_control->window_size = 500;
    rate_control->initial_qp = encoder->yac_qi;
    rate_control->min_qp = 1;
  }

  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

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
  pic_param->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

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
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);
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
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp8_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);
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
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);
  GstVaapiEncoderStatus status;

  status = ensure_profile (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  if (!ensure_bitrate (encoder))
    goto error;

  return set_context_info (base_encoder);

  /* ERRORS */
error:
  {
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

static gboolean
gst_vaapi_encoder_vp8_init (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);

  encoder->frame_num = 0;
  encoder->last_ref = NULL;
  encoder->golden_ref = NULL;
  encoder->alt_ref = NULL;

  return TRUE;
}

static void
gst_vaapi_encoder_vp8_finalize (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);
  clear_references (encoder);
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp8_set_property (GstVaapiEncoder * base_encoder,
    gint prop_id, const GValue * value)
{
  GstVaapiEncoderVP8 *const encoder = GST_VAAPI_ENCODER_VP8_CAST (base_encoder);

  switch (prop_id) {
    case GST_VAAPI_ENCODER_VP8_PROP_LOOP_FILTER_LEVEL:
      encoder->loop_filter_level = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_VP8_PROP_SHARPNESS_LEVEL:
      encoder->sharpness_level = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_VP8_PROP_YAC_Q_INDEX:
      encoder->yac_qi = g_value_get_uint (value);
      break;
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (VP8);

static inline const GstVaapiEncoderClass *
gst_vaapi_encoder_vp8_class (void)
{
  static const GstVaapiEncoderClass GstVaapiEncoderVP8Class = {
    GST_VAAPI_ENCODER_CLASS_INIT (VP8, vp8),
    .set_property = gst_vaapi_encoder_vp8_set_property,
  };
  return &GstVaapiEncoderVP8Class;
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
  return gst_vaapi_encoder_new (gst_vaapi_encoder_vp8_class (), display);
}

/**
 * gst_vaapi_encoder_vp8_get_default_properties:
 *
 * Determines the set of common and vp8 specific encoder properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of encoder properties for #GstVaapiEncoderVP8,
 *   or %NULL if an error occurred.
 */
GPtrArray *
gst_vaapi_encoder_vp8_get_default_properties (void)
{
  const GstVaapiEncoderClass *const klass = gst_vaapi_encoder_vp8_class ();
  GPtrArray *props;

  props = gst_vaapi_encoder_properties_get_default (klass);
  if (!props)
    return NULL;

  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_VP8_PROP_LOOP_FILTER_LEVEL,
      g_param_spec_uint ("loop-filter-level",
          "Loop Filter Level",
          "Controls the deblocking filter strength",
          0, 63, DEFAULT_LOOP_FILTER_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_VP8_PROP_SHARPNESS_LEVEL,
      g_param_spec_uint ("sharpness-level",
          "Sharpness Level",
          "Controls the deblocking filter sensitivity",
          0, 7, DEFAULT_SHARPNESS_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_VP8_PROP_YAC_Q_INDEX,
      g_param_spec_uint ("yac-qi",
          "Luma AC Quant Table index",
          "Quantization Table index for Luma AC Coefficients, (in default case, yac_qi=4 for key frames and yac_qi=40 for P frames)",
          0, 127, DEFAULT_YAC_QI, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  return props;
}
