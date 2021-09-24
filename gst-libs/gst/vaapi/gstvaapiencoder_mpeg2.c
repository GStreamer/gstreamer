/*
 *  gstvaapiencoder_mpeg2.c - MPEG-2 encoder
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Guangxin Xu <guangxin.xu@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#include <math.h>
#include "sysdeps.h"
#include <gst/base/gstbitwriter.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_mpeg2.h"
#include "gstvaapiencoder_mpeg2_priv.h"
#include "gstvaapiutils_mpeg2_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                  \
  (GST_VAAPI_RATECONTROL_MASK (CQP)  |          \
   GST_VAAPI_RATECONTROL_MASK (CBR))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_SEQUENCE |              \
   VA_ENC_PACKED_HEADER_PICTURE)

static gboolean
gst_bit_writer_write_sps (GstBitWriter * bitwriter,
    const VAEncSequenceParameterBufferMPEG2 * seq_param);

static gboolean
gst_bit_writer_write_pps (GstBitWriter * bitwriter,
    const VAEncPictureParameterBufferMPEG2 * pic_param);

static void clear_references (GstVaapiEncoderMpeg2 * encoder);

static void push_reference (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiSurfaceProxy * ref);

/* Derives the profile supported by the underlying hardware */
static gboolean
ensure_hw_profile (GstVaapiEncoderMpeg2 * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  GstVaapiProfile profile, profiles[2];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = encoder->profile;
  switch (encoder->profile) {
    case GST_VAAPI_PROFILE_MPEG2_SIMPLE:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_MPEG2_MAIN;
      break;
    default:
      break;
  }

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

/* Derives the minimum profile from the active coding tools */
static gboolean
ensure_profile (GstVaapiEncoderMpeg2 * encoder)
{
  GstVaapiProfile profile;

  /* Always start from "simple" profile for maximum compatibility */
  profile = GST_VAAPI_PROFILE_MPEG2_SIMPLE;

  /* Main profile coding tools */
  if (encoder->ip_period > 0)
    profile = GST_VAAPI_PROFILE_MPEG2_MAIN;

  encoder->profile = profile;
  encoder->profile_idc = gst_vaapi_utils_mpeg2_get_profile_idc (profile);
  return TRUE;
}

/* Derives the minimum level from the current configuration */
static gboolean
ensure_level (GstVaapiEncoderMpeg2 * encoder)
{
  const GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);
  const guint fps = (vip->fps_n + vip->fps_d - 1) / vip->fps_d;
  const guint bitrate = GST_VAAPI_ENCODER_CAST (encoder)->bitrate;
  const GstVaapiMPEG2LevelLimits *limits_table;
  guint i, num_limits, num_samples;

  num_samples = gst_util_uint64_scale_int_ceil (vip->width * vip->height,
      vip->fps_n, vip->fps_d);

  limits_table = gst_vaapi_utils_mpeg2_get_level_limits_table (&num_limits);
  for (i = 0; i < num_limits; i++) {
    const GstVaapiMPEG2LevelLimits *const limits = &limits_table[i];
    if (vip->width <= limits->horizontal_size_value &&
        vip->height <= limits->vertical_size_value &&
        fps <= limits->frame_rate_value &&
        num_samples <= limits->sample_rate &&
        (!bitrate || bitrate <= limits->bit_rate))
      break;
  }
  if (i == num_limits)
    goto error_unsupported_level;

  encoder->level = limits_table[i].level;
  encoder->level_idc = limits_table[i].level_idc;
  return TRUE;

  /* ERRORS */
error_unsupported_level:
  {
    GST_ERROR ("failed to find a suitable level matching codec config");
    return FALSE;
  }
}

/* Derives the profile and level that suits best to the configuration */
static GstVaapiEncoderStatus
ensure_profile_and_level (GstVaapiEncoderMpeg2 * encoder)
{
  if (!ensure_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  if (!ensure_level (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static gboolean
ensure_bitrate (GstVaapiEncoderMpeg2 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  /* Default compression: 64 bits per macroblock */
  switch (GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    case GST_VAAPI_RATECONTROL_CBR:
      if (!base_encoder->bitrate)
        base_encoder->bitrate =
            gst_util_uint64_scale (GST_VAAPI_ENCODER_WIDTH (encoder) *
            GST_VAAPI_ENCODER_HEIGHT (encoder),
            GST_VAAPI_ENCODER_FPS_N (encoder),
            GST_VAAPI_ENCODER_FPS_D (encoder)) / 4 / 1000;
      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }
  return TRUE;
}

static gboolean
fill_sequence (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncSequence * sequence)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  VAEncSequenceParameterBufferMPEG2 *const seq_param = sequence->param;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferMPEG2));

  seq_param->intra_period = base_encoder->keyframe_period;
  seq_param->ip_period = encoder->ip_period;
  seq_param->picture_width = GST_VAAPI_ENCODER_WIDTH (encoder);
  seq_param->picture_height = GST_VAAPI_ENCODER_HEIGHT (encoder);

  if (base_encoder->bitrate > 0)
    seq_param->bits_per_second = base_encoder->bitrate * 1000;
  else
    seq_param->bits_per_second = 0;

  if (GST_VAAPI_ENCODER_FPS_D (encoder))
    seq_param->frame_rate =
        GST_VAAPI_ENCODER_FPS_N (encoder) / GST_VAAPI_ENCODER_FPS_D (encoder);
  else
    seq_param->frame_rate = 0;

  seq_param->aspect_ratio_information = 1;
  seq_param->vbv_buffer_size = 3;       /* B = 16 * 1024 * vbv_buffer_size */

  seq_param->sequence_extension.bits.profile_and_level_indication =
      (encoder->profile_idc << 4) | encoder->level_idc;
  seq_param->sequence_extension.bits.progressive_sequence = 1;  /* progressive frame-pictures */
  seq_param->sequence_extension.bits.chroma_format =
      gst_vaapi_utils_mpeg2_get_chroma_format_idc
      (GST_VAAPI_CHROMA_TYPE_YUV420);
  seq_param->sequence_extension.bits.low_delay = 0;     /* FIXME */
  seq_param->sequence_extension.bits.frame_rate_extension_n = 0;        /* FIXME */
  seq_param->sequence_extension.bits.frame_rate_extension_d = 0;

  seq_param->gop_header.bits.time_code = (1 << 12);     /* bit12: marker_bit */
  seq_param->gop_header.bits.closed_gop = 0;
  seq_param->gop_header.bits.broken_link = 0;

  return TRUE;
}

static VAEncPictureType
get_va_enc_picture_type (GstVaapiPictureType type)
{
  switch (type) {
    case GST_VAAPI_PICTURE_TYPE_I:
      return VAEncPictureTypeIntra;
    case GST_VAAPI_PICTURE_TYPE_P:
      return VAEncPictureTypePredictive;
    case GST_VAAPI_PICTURE_TYPE_B:
      return VAEncPictureTypeBidirectional;
    default:
      return -1;
  }
  return -1;
}

static gboolean
fill_picture (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferMPEG2 *const pic_param = picture->param;
  guint8 f_code_x, f_code_y;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferMPEG2));

  pic_param->reconstructed_picture =
      GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->coded_buf = GST_VAAPI_CODED_BUFFER_ID (codedbuf);
  pic_param->picture_type = get_va_enc_picture_type (picture->type);
  pic_param->temporal_reference = picture->frame_num & (1024 - 1);
  pic_param->vbv_delay = 0xFFFF;

  f_code_x = 0xf;
  f_code_y = 0xf;
  if (pic_param->picture_type != VAEncPictureTypeIntra) {
    switch (encoder->level) {
      case GST_VAAPI_LEVEL_MPEG2_LOW:
        f_code_x = 7;
        f_code_y = 4;
        break;
      case GST_VAAPI_LEVEL_MPEG2_MAIN:
        f_code_x = 8;
        f_code_y = 5;
        break;
      default:                 /* High-1440 and High levels */
        f_code_x = 9;
        f_code_y = 5;
        break;
    }
  }

  if (pic_param->picture_type == VAEncPictureTypeIntra) {
    pic_param->f_code[0][0] = 0xf;
    pic_param->f_code[0][1] = 0xf;
    pic_param->f_code[1][0] = 0xf;
    pic_param->f_code[1][1] = 0xf;
    pic_param->forward_reference_picture = VA_INVALID_SURFACE;
    pic_param->backward_reference_picture = VA_INVALID_SURFACE;
  } else if (pic_param->picture_type == VAEncPictureTypePredictive) {
    pic_param->f_code[0][0] = f_code_x;
    pic_param->f_code[0][1] = f_code_y;
    pic_param->f_code[1][0] = 0xf;
    pic_param->f_code[1][1] = 0xf;
    pic_param->forward_reference_picture =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->forward);
    pic_param->backward_reference_picture = VA_INVALID_SURFACE;
  } else if (pic_param->picture_type == VAEncPictureTypeBidirectional) {
    pic_param->f_code[0][0] = f_code_x;
    pic_param->f_code[0][1] = f_code_y;
    pic_param->f_code[1][0] = f_code_x;
    pic_param->f_code[1][1] = f_code_y;
    pic_param->forward_reference_picture =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->forward);
    pic_param->backward_reference_picture =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->backward);
  } else {
    g_assert (0);
  }

  pic_param->picture_coding_extension.bits.intra_dc_precision = 0;      /* 8bits */
  pic_param->picture_coding_extension.bits.picture_structure = 3;       /* frame picture */
  pic_param->picture_coding_extension.bits.top_field_first = 0;
  pic_param->picture_coding_extension.bits.frame_pred_frame_dct = 1;    /* FIXME */
  pic_param->picture_coding_extension.bits.concealment_motion_vectors = 0;
  pic_param->picture_coding_extension.bits.q_scale_type = 0;
  pic_param->picture_coding_extension.bits.intra_vlc_format = 0;
  pic_param->picture_coding_extension.bits.alternate_scan = 0;
  pic_param->picture_coding_extension.bits.repeat_first_field = 0;
  pic_param->picture_coding_extension.bits.progressive_frame = 1;
  pic_param->picture_coding_extension.bits.composite_display_flag = 0;

  return TRUE;
}

static gboolean
set_sequence_packed_header (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_seq;
  GstBitWriter writer;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  const VAEncSequenceParameterBufferMPEG2 *const seq_param = sequence->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init_with_size (&writer, 128, FALSE);
  if (encoder->new_gop)
    gst_bit_writer_write_sps (&writer, seq_param);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&writer) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&writer);
  data = GST_BIT_WRITER_DATA (&writer);

  packed_header_param_buffer.type = VAEncPackedHeaderSequence;
  packed_header_param_buffer.bit_length = data_bit_size;
  packed_header_param_buffer.has_emulation_bytes = 0;

  packed_seq = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_header_param_buffer, sizeof (packed_header_param_buffer),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_seq);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_seq);
  gst_vaapi_codec_object_replace (&packed_seq, NULL);
  gst_bit_writer_reset (&writer);

  return TRUE;
}

static gboolean
set_picture_packed_header (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncPackedHeader *packed_pic;
  GstBitWriter writer;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  const VAEncPictureParameterBufferMPEG2 *const pic_param = picture->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init_with_size (&writer, 128, FALSE);
  gst_bit_writer_write_pps (&writer, pic_param);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&writer) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&writer);
  data = GST_BIT_WRITER_DATA (&writer);

  packed_header_param_buffer.type = VAEncPackedHeaderPicture;
  packed_header_param_buffer.bit_length = data_bit_size;
  packed_header_param_buffer.has_emulation_bytes = 0;

  packed_pic = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_header_param_buffer, sizeof (packed_header_param_buffer),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_pic);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_pic);
  gst_vaapi_codec_object_replace (&packed_pic, NULL);
  gst_bit_writer_reset (&writer);

  return TRUE;
}

static gboolean
ensure_sequence (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence;

  g_assert (picture);
  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (MPEG2, encoder);
  g_assert (sequence);
  if (!sequence)
    goto error;

  if (!fill_sequence (encoder, sequence))
    goto error;

  if ((GST_VAAPI_ENCODER_PACKED_HEADERS (encoder) &
          VA_ENC_PACKED_HEADER_SEQUENCE)
      && picture->type == GST_VAAPI_PICTURE_TYPE_I
      && !set_sequence_packed_header (encoder, picture, sequence))
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
ensure_picture (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_picture (encoder, picture, codedbuf, surface))
    return FALSE;

  if ((GST_VAAPI_ENCODER_PACKED_HEADERS (encoder) &
          VA_ENC_PACKED_HEADER_PICTURE)
      && !set_picture_packed_header (encoder, picture)) {
    GST_ERROR ("set picture packed header failed");
    return FALSE;
  }
  return TRUE;
}

static gboolean
ensure_control_rate_params (GstVaapiEncoderMpeg2 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CQP)
    return TRUE;

  /* RateControl params */
  GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder).initial_qp = encoder->cqp;

  /* *INDENT-OFF* */
  /* HRD params */
  GST_VAAPI_ENCODER_VA_HRD (encoder) = (VAEncMiscParameterHRD) {
    .buffer_size = base_encoder->bitrate * 1000 * 8,
    .initial_buffer_fullness = base_encoder->bitrate * 1000 * 4,
  };
  /* *INDENT-ON* */

  return TRUE;
}

static gboolean
set_misc_parameters (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  if (!gst_vaapi_encoder_ensure_param_control_rate (base_encoder, picture))
    return FALSE;
  if (!gst_vaapi_encoder_ensure_param_quality_level (base_encoder, picture))
    return FALSE;
  return TRUE;
}

static gboolean
fill_slices (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncPicture * picture)
{
  VAEncSliceParameterBufferMPEG2 *slice_param;
  GstVaapiEncSlice *slice;
  guint width_in_mbs, height_in_mbs;
  guint i_slice;

  g_assert (picture);

  width_in_mbs = (GST_VAAPI_ENCODER_WIDTH (encoder) + 15) / 16;
  height_in_mbs = (GST_VAAPI_ENCODER_HEIGHT (encoder) + 15) / 16;

  for (i_slice = 0; i_slice < height_in_mbs; ++i_slice) {
    slice = GST_VAAPI_ENC_SLICE_NEW (MPEG2, encoder);
    g_assert (slice && slice->param_id != VA_INVALID_ID);
    slice_param = slice->param;

    memset (slice_param, 0, sizeof (VAEncSliceParameterBufferMPEG2));

    slice_param->macroblock_address = i_slice * width_in_mbs;
    slice_param->num_macroblocks = width_in_mbs;
    slice_param->is_intra_slice = (picture->type == GST_VAAPI_PICTURE_TYPE_I);
    slice_param->quantiser_scale_code = encoder->cqp / 2;

    gst_vaapi_enc_picture_add_slice (picture, slice);
    gst_vaapi_codec_object_replace (&slice, NULL);
  }
  return TRUE;
}

static gboolean
ensure_slices (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncPicture * picture)
{
  g_assert (picture);

  if (!fill_slices (encoder, picture))
    return FALSE;

  return TRUE;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderMpeg2 *const encoder =
      GST_VAAPI_ENCODER_MPEG2_CAST (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base_encoder);

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));

  if (!ensure_sequence (encoder, picture))
    goto error;
  if (!ensure_picture (encoder, picture, codedbuf, reconstruct))
    goto error;
  if (!set_misc_parameters (encoder, picture))
    goto error;
  if (!ensure_slices (encoder, picture))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_B) {
    if (encoder->new_gop)
      clear_references (encoder);
    push_reference (encoder, reconstruct);
  } else if (reconstruct)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
        reconstruct);

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
gst_vaapi_encoder_mpeg2_flush (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderMpeg2 *const encoder =
      GST_VAAPI_ENCODER_MPEG2_CAST (base_encoder);
  GstVaapiEncPicture *pic;

  while (!g_queue_is_empty (&encoder->b_frames)) {
    pic = g_queue_pop_head (&encoder->b_frames);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->b_frames);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderMpeg2 *const encoder =
      GST_VAAPI_ENCODER_MPEG2_CAST (base_encoder);
  GstVaapiEncPicture *picture = NULL;
  GstVaapiEncoderStatus status = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  if (!frame) {
    if (g_queue_is_empty (&encoder->b_frames) && encoder->dump_frames) {
      push_reference (encoder, NULL);
      encoder->dump_frames = FALSE;
    }
    if (!encoder->dump_frames) {
      return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;
    }
    picture = g_queue_pop_head (&encoder->b_frames);
    g_assert (picture);
    goto end;
  }

  picture = GST_VAAPI_ENC_PICTURE_NEW (MPEG2, encoder, frame);
  if (!picture) {
    GST_WARNING ("create MPEG2 picture failed, frame timestamp:%"
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
    encoder->new_gop = TRUE;
  } else {
    encoder->new_gop = FALSE;
    if ((encoder->frame_num % (encoder->ip_period + 1)) == 0 ||
        encoder->frame_num == base_encoder->keyframe_period - 1) {
      picture->type = GST_VAAPI_PICTURE_TYPE_P;
      encoder->dump_frames = TRUE;
    } else {
      picture->type = GST_VAAPI_PICTURE_TYPE_B;
      status = GST_VAAPI_ENCODER_STATUS_NO_SURFACE;
    }
  }
  picture->frame_num = encoder->frame_num++;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_B) {
    g_queue_push_tail (&encoder->b_frames, picture);
    picture = NULL;
  }

end:
  *output = picture;
  return status;
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderMpeg2 *const encoder =
      GST_VAAPI_ENCODER_MPEG2_CAST (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);

  /* Maximum sizes for common headers (in bytes) */
  enum
  {
    MAX_SEQ_HDR_SIZE = 140,
    MAX_SEQ_EXT_SIZE = 10,
    MAX_GOP_SIZE = 8,
    MAX_PIC_HDR_SIZE = 10,
    MAX_PIC_EXT_SIZE = 11,
    MAX_SLICE_HDR_SIZE = 8,
  };

  if (!ensure_hw_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames = 2;

  /* Only YUV 4:2:0 formats are supported for now. This means that we
     have a limit of 4608 bits per macroblock. */
  base_encoder->codedbuf_size = (GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) / 256) * 576;

  /* Account for Sequence, GOP, and Picture headers */
  /* XXX: exclude unused Sequence Display Extension, Sequence Scalable
     Extension, Quantization Matrix Extension, Picture Display Extension,
     Picture Temporal Scalable Extension, Picture Spatial Scalable
     Extension */
  base_encoder->codedbuf_size += MAX_SEQ_HDR_SIZE + MAX_SEQ_EXT_SIZE +
      MAX_GOP_SIZE + MAX_PIC_HDR_SIZE + MAX_PIC_EXT_SIZE;

  /* Account for Slice headers. We use one slice per line of macroblock */
  base_encoder->codedbuf_size += (GST_ROUND_UP_16 (vip->height) / 16) *
      MAX_SLICE_HDR_SIZE;

  base_encoder->context_info.profile = base_encoder->profile;
  base_encoder->context_info.entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderMpeg2 *const encoder =
      GST_VAAPI_ENCODER_MPEG2_CAST (base_encoder);
  GstVaapiEncoderStatus status;

  if (encoder->ip_period > base_encoder->keyframe_period) {
    encoder->ip_period = base_encoder->keyframe_period - 1;
  }

  status = ensure_profile_and_level (encoder);
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

struct _GstVaapiEncoderMpeg2Class
{
  GstVaapiEncoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiEncoderMpeg2, gst_vaapi_encoder_mpeg2,
    GST_TYPE_VAAPI_ENCODER);

static void
gst_vaapi_encoder_mpeg2_init (GstVaapiEncoderMpeg2 * encoder)
{
  /* re-ordering */
  g_queue_init (&encoder->b_frames);
}

static void
clear_ref (GstVaapiEncoderMpeg2 * encoder, GstVaapiSurfaceProxy ** ref)
{
  if (*ref) {
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder), *ref);
    *ref = NULL;
  }
}

static void
clear_references (GstVaapiEncoderMpeg2 * encoder)
{
  clear_ref (encoder, &encoder->forward);
  clear_ref (encoder, &encoder->backward);
}

static void
push_reference (GstVaapiEncoderMpeg2 * encoder, GstVaapiSurfaceProxy * ref)
{
  if (encoder->backward) {
    clear_ref (encoder, &encoder->forward);
    encoder->forward = encoder->backward;
    encoder->backward = NULL;
  }
  if (encoder->forward)
    encoder->backward = ref;
  else
    encoder->forward = ref;
}

static void
gst_vaapi_encoder_mpeg2_finalize (GObject * object)
{
  /* free private buffers */
  GstVaapiEncoderMpeg2 *const encoder = GST_VAAPI_ENCODER_MPEG2 (object);
  GstVaapiEncPicture *pic;

  clear_references (encoder);

  while (!g_queue_is_empty (&encoder->b_frames)) {
    pic = g_queue_pop_head (&encoder->b_frames);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->b_frames);

  G_OBJECT_CLASS (gst_vaapi_encoder_mpeg2_parent_class)->finalize (object);
}

/**
 * @ENCODER_MPEG2_PROP_RATECONTROL: Rate control (#GstVaapiRateControl).
 * @ENCODER_MPEG2_PROP_TUNE: The tuning options (#GstVaapiEncoderTune).
 * @ENCODER_MPEG2_PROP_QUANTIZER: Constant quantizer value (uint).
 * @ENCODER_MPEG2_PROP_MAX_BFRAMES: Number of B-frames between I
 *   and P (uint).
 *
 * The set of MPEG-2 encoder specific configurable properties.
 */
enum
{
  ENCODER_MPEG2_PROP_RATECONTROL = 1,
  ENCODER_MPEG2_PROP_TUNE,
  ENCODER_MPEG2_PROP_QUANTIZER,
  ENCODER_MPEG2_PROP_MAX_BFRAMES,
  ENCODER_MPEG2_N_PROPERTIES
};

static GParamSpec *properties[ENCODER_MPEG2_N_PROPERTIES];

static void
gst_vaapi_encoder_mpeg2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);
  GstVaapiEncoderMpeg2 *const encoder = GST_VAAPI_ENCODER_MPEG2 (object);

  if (base_encoder->num_codedbuf_queued > 0) {
    GST_ERROR_OBJECT (object,
        "failed to set any property after encoding started");
    return;
  }

  switch (prop_id) {
    case ENCODER_MPEG2_PROP_RATECONTROL:
      gst_vaapi_encoder_set_rate_control (base_encoder,
          g_value_get_enum (value));
      break;
    case ENCODER_MPEG2_PROP_TUNE:
      gst_vaapi_encoder_set_tuning (base_encoder, g_value_get_enum (value));
      break;
    case ENCODER_MPEG2_PROP_QUANTIZER:
      encoder->cqp = g_value_get_uint (value);
      break;
    case ENCODER_MPEG2_PROP_MAX_BFRAMES:
      encoder->ip_period = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_vaapi_encoder_mpeg2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoderMpeg2 *const encoder = GST_VAAPI_ENCODER_MPEG2 (object);
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);

  switch (prop_id) {
    case ENCODER_MPEG2_PROP_RATECONTROL:
      g_value_set_enum (value, base_encoder->rate_control);
      break;
    case ENCODER_MPEG2_PROP_TUNE:
      g_value_set_enum (value, base_encoder->tune);
      break;
    case ENCODER_MPEG2_PROP_QUANTIZER:
      g_value_set_uint (value, encoder->cqp);
      break;
    case ENCODER_MPEG2_PROP_MAX_BFRAMES:
      g_value_set_uint (value, encoder->ip_period);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (MPEG2);

static void
gst_vaapi_encoder_mpeg2_class_init (GstVaapiEncoderMpeg2Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiEncoderClass *const encoder_class = GST_VAAPI_ENCODER_CLASS (klass);

  encoder_class->class_data = &g_class_data;
  encoder_class->reconfigure = gst_vaapi_encoder_mpeg2_reconfigure;
  encoder_class->reordering = gst_vaapi_encoder_mpeg2_reordering;
  encoder_class->encode = gst_vaapi_encoder_mpeg2_encode;
  encoder_class->flush = gst_vaapi_encoder_mpeg2_flush;

  object_class->set_property = gst_vaapi_encoder_mpeg2_set_property;
  object_class->get_property = gst_vaapi_encoder_mpeg2_get_property;
  object_class->finalize = gst_vaapi_encoder_mpeg2_finalize;

  /**
   * GstVaapiEncoderMpeg2:rate-control:
   *
   * The desired rate control mode, expressed as a #GstVaapiRateControl.
   */
  properties[ENCODER_MPEG2_PROP_RATECONTROL] =
      g_param_spec_enum ("rate-control",
      "Rate Control", "Rate control mode",
      g_class_data.rate_control_get_type (),
      g_class_data.default_rate_control,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVaapiEncoderMpeg2:tune:
   *
   * The desired encoder tuning option.
   */
  properties[ENCODER_MPEG2_PROP_TUNE] =
      g_param_spec_enum ("tune",
      "Encoder Tuning",
      "Encoder tuning option",
      g_class_data.encoder_tune_get_type (),
      g_class_data.default_encoder_tune,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_MPEG2_PROP_QUANTIZER] =
      g_param_spec_uint ("quantizer",
      "Constant Quantizer",
      "Constant quantizer (if rate-control mode is CQP)",
      2, 62, 8, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_MPEG2_PROP_MAX_BFRAMES] =
      g_param_spec_uint ("max-bframes", "Max B-Frames",
      "Number of B-frames between I and P", 0, 16, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  g_object_class_install_properties (object_class, ENCODER_MPEG2_N_PROPERTIES,
      properties);

  gst_type_mark_as_plugin_api (g_class_data.rate_control_get_type (), 0);
  gst_type_mark_as_plugin_api (g_class_data.encoder_tune_get_type (), 0);
}

/**
 * gst_vaapi_encoder_mpeg2_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for MPEG-2 encoding.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_mpeg2_new (GstVaapiDisplay * display)
{
  return g_object_new (GST_TYPE_VAAPI_ENCODER_MPEG2, "display", display, NULL);
}

static struct
{
  int code;
  float value;
} frame_rate_tab[] = {
  /* *INDENT-OFF* */
  { 1, 23.976 },
  { 2, 24.0   },
  { 3, 25.0   },
  { 4, 29.97  },
  { 5, 30     },
  { 6, 50     },
  { 7, 59.94  },
  { 8, 60     }
  /* *INDENT-ON* */
};

static int
find_frame_rate_code (const VAEncSequenceParameterBufferMPEG2 * seq_param)
{
  unsigned int ndelta, delta = -1;
  int code = 1, i;
  float frame_rate_value = seq_param->frame_rate *
      (seq_param->sequence_extension.bits.frame_rate_extension_d + 1) /
      (seq_param->sequence_extension.bits.frame_rate_extension_n + 1);

  for (i = 0; i < G_N_ELEMENTS (frame_rate_tab); i++) {

    ndelta = fabsf (1000 * frame_rate_tab[i].value - 1000 * frame_rate_value);
    if (ndelta < delta) {
      code = frame_rate_tab[i].code;
      delta = ndelta;
    }
  }
  return code;
}

static gboolean
gst_bit_writer_write_sps (GstBitWriter * bitwriter,
    const VAEncSequenceParameterBufferMPEG2 * seq_param)
{
  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_SEQ, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->picture_width, 12);
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->picture_height, 12);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->aspect_ratio_information, 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, find_frame_rate_code (seq_param), 4);      /* frame_rate_code */
  gst_bit_writer_put_bits_uint32 (bitwriter, (seq_param->bits_per_second + 399) / 400, 18);     /* the low 18 bits of bit_rate */
  gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);     /* marker_bit */
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->vbv_buffer_size, 10);
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);     /* constraint_parameter_flag, always 0 for MPEG-2 */
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);     /* load_intra_quantiser_matrix */
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);     /* load_non_intra_quantiser_matrix */

  gst_bit_writer_align_bytes (bitwriter, 0);

  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_EXT, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter, 1, 4);     /* sequence_extension id */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->sequence_extension.bits.profile_and_level_indication, 8);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->sequence_extension.bits.progressive_sequence, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->sequence_extension.bits.chroma_format, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->picture_width >> 12, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->picture_height >> 12,
      2);
  gst_bit_writer_put_bits_uint32 (bitwriter, ((seq_param->bits_per_second + 399) / 400) >> 18, 12);     /* bit_rate_extension */
  gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);     /* marker_bit */
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->vbv_buffer_size >> 10,
      8);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->sequence_extension.bits.low_delay, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->sequence_extension.bits.frame_rate_extension_n, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->sequence_extension.bits.frame_rate_extension_d, 5);

  gst_bit_writer_align_bytes (bitwriter, 0);

  /* gop header */
  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_GOP, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->gop_header.bits.time_code, 25);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->gop_header.bits.closed_gop, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->gop_header.bits.broken_link, 1);

  gst_bit_writer_align_bytes (bitwriter, 0);

  return TRUE;
}

static gboolean
gst_bit_writer_write_pps (GstBitWriter * bitwriter,
    const VAEncPictureParameterBufferMPEG2 * pic_param)
{
  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_PICUTRE, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic_param->temporal_reference, 10);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_type == VAEncPictureTypeIntra ? 1 :
      pic_param->picture_type == VAEncPictureTypePredictive ? 2 : 3, 3);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic_param->vbv_delay, 16);

  if (pic_param->picture_type == VAEncPictureTypePredictive ||
      pic_param->picture_type == VAEncPictureTypeBidirectional) {
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* full_pel_forward_vector, always 0 for MPEG-2 */
    gst_bit_writer_put_bits_uint32 (bitwriter, 7, 3);   /* forward_f_code, always 7 for MPEG-2 */
  }

  if (pic_param->picture_type == VAEncPictureTypeBidirectional) {
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* full_pel_backward_vector, always 0 for MPEG-2 */
    gst_bit_writer_put_bits_uint32 (bitwriter, 7, 3);   /* backward_f_code, always 7 for MPEG-2 */
  }

  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);     /* extra_bit_picture, 0 */

  gst_bit_writer_align_bytes (bitwriter, 0);

  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_EXT, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter, 8, 4);     /* Picture Coding Extension ID: 8 */
  gst_bit_writer_put_bits_uint32 (bitwriter, pic_param->f_code[0][0], 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic_param->f_code[0][1], 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic_param->f_code[1][0], 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic_param->f_code[1][1], 4);

  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.intra_dc_precision, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.picture_structure, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.top_field_first, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.frame_pred_frame_dct, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.concealment_motion_vectors, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.q_scale_type, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.intra_vlc_format, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.alternate_scan, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.repeat_first_field, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);     /* always chroma 420 */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.progressive_frame, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->picture_coding_extension.bits.composite_display_flag, 1);

  gst_bit_writer_align_bytes (bitwriter, 0);

  return TRUE;
}
