/*
 *  gstvaapiencoder_mpeg2.c - MPEG-2 encoder
 *
 *  Copyright (C) 2012 -2013 Intel Corporation
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
#include "gstvaapicompat.h"
#include "gstvaapiencoder_mpeg2.h"
#include "gstvaapiencoder_mpeg2_priv.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"

#include <va/va.h>
#include <va/va_enc_mpeg2.h>

#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                  \
  (GST_VAAPI_RATECONTROL_MASK (NONE) |          \
   GST_VAAPI_RATECONTROL_MASK (CQP)  |          \
   GST_VAAPI_RATECONTROL_MASK (CBR))

static gboolean
gst_bit_writer_write_sps (GstBitWriter * bitwriter,
    VAEncSequenceParameterBufferMPEG2 * seq, GstVaapiEncoderMpeg2 * encoder);

static gboolean
gst_bit_writer_write_pps (GstBitWriter * bitwriter,
    VAEncPictureParameterBufferMPEG2 * pic);


static void clear_references (GstVaapiEncoderMpeg2 * encoder);

static void push_reference (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiSurfaceProxy * ref);

static struct
{
  int samplers_per_line;
  int line_per_frame;
  int frame_per_sec;
} mpeg2_upper_samplings[2][3] = {
  /* *INDENT-OFF* */
  { { 0, 0, 0},
    { 720, 576, 30 },
    { 0, 0, 0 },
  },
  { { 352, 288, 30 },
    { 720, 576, 30 },
    { 1920, 1152, 60 },
  }
  /* *INDENT-ON* */
};

static gboolean
ensure_sampling_desity (GstVaapiEncoderMpeg2 * encoder)
{
  guint p, l;
  float fps;

  p = encoder->profile;
  l = encoder->level;
  fps = GST_VAAPI_ENCODER_FPS_N (encoder) / GST_VAAPI_ENCODER_FPS_D (encoder);
  if (mpeg2_upper_samplings[p][l].samplers_per_line <
      GST_VAAPI_ENCODER_WIDTH (encoder)
      || mpeg2_upper_samplings[p][l].line_per_frame <
      GST_VAAPI_ENCODER_HEIGHT (encoder)
      || mpeg2_upper_samplings[p][l].frame_per_sec < fps) {
    GST_ERROR
        ("acording to slected profile(%d) and level(%d) the max resolution is %dx%d@%d",
        p, l, mpeg2_upper_samplings[p][l].samplers_per_line,
        mpeg2_upper_samplings[p][l].line_per_frame,
        mpeg2_upper_samplings[p][l].frame_per_sec);
    return FALSE;
  }
  return TRUE;
}

static gboolean
ensure_profile_and_level (GstVaapiEncoderMpeg2 * encoder)
{
  if (encoder->profile == GST_ENCODER_MPEG2_PROFILE_SIMPLE) {
    /* no  b frames */
    encoder->ip_period = 0;
    /* only main level is defined in mpeg2 */
    encoder->level = GST_VAAPI_ENCODER_MPEG2_LEVEL_MAIN;
  }
  return TRUE;
}

static gboolean
ensure_bitrate (GstVaapiEncoderMpeg2 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  /* Default compression: 64 bits per macroblock */
  switch (GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    case GST_VAAPI_RATECONTROL_CBR:
      if (!base_encoder->bitrate)
        base_encoder->bitrate = GST_VAAPI_ENCODER_WIDTH (encoder) *
            GST_VAAPI_ENCODER_HEIGHT (encoder) *
            GST_VAAPI_ENCODER_FPS_N (encoder) /
            GST_VAAPI_ENCODER_FPS_D (encoder) / 4 / 1024;
      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }
  return TRUE;
}

static unsigned char
make_profile_and_level_indication (guint32 profile, guint32 level)
{
  guint32 p = 4, l = 8;

  switch (profile) {
    case GST_ENCODER_MPEG2_PROFILE_SIMPLE:
      p = 5;
      break;
    case GST_ENCODER_MPEG2_PROFILE_MAIN:
      p = 4;
      break;
    default:
      g_assert (0);
      break;
  }

  switch (level) {
    case GST_VAAPI_ENCODER_MPEG2_LEVEL_LOW:
      l = 10;
      break;
    case GST_VAAPI_ENCODER_MPEG2_LEVEL_MAIN:
      l = 8;
      break;
    case GST_VAAPI_ENCODER_MPEG2_LEVEL_HIGH:
      l = 4;
      break;
    default:
      g_assert (0);
      break;
  }
  return p << 4 | l;
}

static gboolean
fill_sequence (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncSequence * sequence)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  VAEncSequenceParameterBufferMPEG2 *seq = sequence->param;

  memset (seq, 0, sizeof (VAEncSequenceParameterBufferMPEG2));

  seq->intra_period = base_encoder->keyframe_period;
  seq->ip_period = encoder->ip_period;
  seq->picture_width = GST_VAAPI_ENCODER_WIDTH (encoder);
  seq->picture_height = GST_VAAPI_ENCODER_HEIGHT (encoder);

  if (base_encoder->bitrate > 0)
    seq->bits_per_second = base_encoder->bitrate * 1024;
  else
    seq->bits_per_second = 0;

  if (GST_VAAPI_ENCODER_FPS_D (encoder))
    seq->frame_rate =
        GST_VAAPI_ENCODER_FPS_N (encoder) / GST_VAAPI_ENCODER_FPS_D (encoder);
  else
    seq->frame_rate = 0;

  seq->aspect_ratio_information = 1;
  seq->vbv_buffer_size = 3;     /* B = 16 * 1024 * vbv_buffer_size */

  seq->sequence_extension.bits.profile_and_level_indication =
      make_profile_and_level_indication (encoder->profile, encoder->level);
  seq->sequence_extension.bits.progressive_sequence = 1;        /* progressive frame-pictures */
  seq->sequence_extension.bits.chroma_format = CHROMA_FORMAT_420;       /* 4:2:0 */
  seq->sequence_extension.bits.low_delay = 0;   /* FIXME */
  seq->sequence_extension.bits.frame_rate_extension_n = 0;      /*FIXME */
  seq->sequence_extension.bits.frame_rate_extension_d = 0;

  seq->gop_header.bits.time_code = (1 << 12);   /* bit12: marker_bit */
  seq->gop_header.bits.closed_gop = 0;
  seq->gop_header.bits.broken_link = 0;

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
  VAEncPictureParameterBufferMPEG2 *pic = picture->param;
  guint8 f_code_x, f_code_y;

  memset (pic, 0, sizeof (VAEncPictureParameterBufferMPEG2));

  pic->reconstructed_picture = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);
  pic->picture_type = get_va_enc_picture_type (picture->type);
  pic->temporal_reference = picture->frame_num & (1024 - 1);
  pic->vbv_delay = 0xFFFF;

  f_code_x = 0xf;
  f_code_y = 0xf;
  if (pic->picture_type != VAEncPictureTypeIntra) {
    if (encoder->level == GST_VAAPI_ENCODER_MPEG2_LEVEL_LOW) {
      f_code_x = 7;
      f_code_y = 4;
    } else if (encoder->level == GST_VAAPI_ENCODER_MPEG2_LEVEL_MAIN) {
      f_code_x = 8;
      f_code_y = 5;
    } else {
      f_code_x = 9;
      f_code_y = 5;
    }
  }

  if (pic->picture_type == VAEncPictureTypeIntra) {
    pic->f_code[0][0] = 0xf;
    pic->f_code[0][1] = 0xf;
    pic->f_code[1][0] = 0xf;
    pic->f_code[1][1] = 0xf;
    pic->forward_reference_picture = VA_INVALID_SURFACE;
    pic->backward_reference_picture = VA_INVALID_SURFACE;
  } else if (pic->picture_type == VAEncPictureTypePredictive) {
    pic->f_code[0][0] = f_code_x;
    pic->f_code[0][1] = f_code_y;
    pic->f_code[1][0] = 0xf;
    pic->f_code[1][1] = 0xf;
    pic->forward_reference_picture =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->forward);
    pic->backward_reference_picture = VA_INVALID_SURFACE;
  } else if (pic->picture_type == VAEncPictureTypeBidirectional) {
    pic->f_code[0][0] = f_code_x;
    pic->f_code[0][1] = f_code_y;
    pic->f_code[1][0] = f_code_x;
    pic->f_code[1][1] = f_code_y;
    pic->forward_reference_picture =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->forward);;
    pic->backward_reference_picture =
        GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->backward);;
  } else {
    g_assert (0);
  }

  pic->picture_coding_extension.bits.intra_dc_precision = 0;    /* 8bits */
  pic->picture_coding_extension.bits.picture_structure = 3;     /* frame picture */
  pic->picture_coding_extension.bits.top_field_first = 0;
  pic->picture_coding_extension.bits.frame_pred_frame_dct = 1;  /* FIXME */
  pic->picture_coding_extension.bits.concealment_motion_vectors = 0;
  pic->picture_coding_extension.bits.q_scale_type = 0;
  pic->picture_coding_extension.bits.intra_vlc_format = 0;
  pic->picture_coding_extension.bits.alternate_scan = 0;
  pic->picture_coding_extension.bits.repeat_first_field = 0;
  pic->picture_coding_extension.bits.progressive_frame = 1;
  pic->picture_coding_extension.bits.composite_display_flag = 0;

  return TRUE;
}

static gboolean
set_sequence_packed_header (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_seq;
  GstBitWriter writer;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  VAEncSequenceParameterBufferMPEG2 *seq = sequence->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init (&writer, 128 * 8);
  gst_bit_writer_write_sps (&writer, seq, encoder);
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
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & packed_seq, NULL);
  gst_bit_writer_clear (&writer, TRUE);

  return TRUE;
}

static gboolean
set_picture_packed_header (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncPackedHeader *packed_pic;
  GstBitWriter writer;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  VAEncPictureParameterBufferMPEG2 *pic = picture->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init (&writer, 128 * 8);
  gst_bit_writer_write_pps (&writer, pic);
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
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & packed_pic, NULL);
  gst_bit_writer_clear (&writer, TRUE);

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

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I &&
      !set_sequence_packed_header (encoder, picture, sequence))
    goto error;
  gst_vaapi_enc_picture_set_sequence (picture, sequence);
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (&sequence), NULL);
  return TRUE;

error:
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (&sequence), NULL);
  return FALSE;
}

static gboolean
ensure_picture (GstVaapiEncoderMpeg2 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_picture (encoder, picture, codedbuf, surface))
    return FALSE;

  if (!set_picture_packed_header (encoder, picture)) {
    GST_ERROR ("set picture packed header failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
set_misc_parameters (GstVaapiEncoderMpeg2 * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  GstVaapiEncMiscParam *misc = NULL;
  VAEncMiscParameterHRD *hrd;
  VAEncMiscParameterRateControl *rate_control;

  /* add hrd */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, encoder);
  g_assert (misc);
  if (!misc)
    return FALSE;
  gst_vaapi_enc_picture_add_misc_buffer (picture, misc);
  hrd = misc->impl;
  if (base_encoder->bitrate > 0) {
    hrd->initial_buffer_fullness = base_encoder->bitrate * 1024 * 4;
    hrd->buffer_size = base_encoder->bitrate * 1024 * 8;
  } else {
    hrd->initial_buffer_fullness = 0;
    hrd->buffer_size = 0;
  }
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & misc, NULL);

  /* add ratecontrol */
  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CBR) {
    misc = GST_VAAPI_ENC_MISC_PARAM_NEW (RateControl, encoder);
    g_assert (misc);
    if (!misc)
      return FALSE;
    gst_vaapi_enc_picture_add_misc_buffer (picture, misc);
    rate_control = misc->impl;
    memset (rate_control, 0, sizeof (VAEncMiscParameterRateControl));
    if (base_encoder->bitrate)
      rate_control->bits_per_second = base_encoder->bitrate * 1024;
    else
      rate_control->bits_per_second = 0;
    rate_control->target_percentage = 70;
    rate_control->window_size = 500;
    rate_control->initial_qp = encoder->cqp;
    rate_control->min_qp = 0;
    rate_control->basic_unit_size = 0;
    gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & misc, NULL);
  }

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
    gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & slice, NULL);

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
gst_vaapi_encoder_mpeg2_encode (GstVaapiEncoder * base,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderMpeg2 *encoder = GST_VAAPI_ENCODER_MPEG2_CAST (base);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base);

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
error:
  if (reconstruct)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
        reconstruct);
  return ret;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_flush (GstVaapiEncoder * base)
{
  GstVaapiEncoderMpeg2 *encoder = GST_VAAPI_ENCODER_MPEG2_CAST (base);
  GstVaapiEncPicture *pic;

  while (!g_queue_is_empty (&encoder->b_frames)) {
    pic = (GstVaapiEncPicture *) g_queue_pop_head (&encoder->b_frames);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->b_frames);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_reordering (GstVaapiEncoder * base,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderMpeg2 *encoder = GST_VAAPI_ENCODER_MPEG2 (base);
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

  if (encoder->frame_num >= base->keyframe_period) {
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
        encoder->frame_num == base->keyframe_period - 1) {
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

static GstVaapiProfile
to_vaapi_profile (guint32 profile)
{
  GstVaapiProfile p;

  switch (profile) {
    case GST_ENCODER_MPEG2_PROFILE_SIMPLE:
      p = GST_VAAPI_PROFILE_MPEG2_SIMPLE;
      break;
    case GST_ENCODER_MPEG2_PROFILE_MAIN:
      p = GST_VAAPI_PROFILE_MPEG2_MAIN;
      break;
    default:
      g_assert (0);
  }
  return p;
}

static void
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderMpeg2 *const encoder = GST_VAAPI_ENCODER_MPEG2 (base_encoder);
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

  base_encoder->profile = to_vaapi_profile (encoder->profile);
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
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderMpeg2 *const encoder =
      GST_VAAPI_ENCODER_MPEG2_CAST (base_encoder);

  if (encoder->ip_period > base_encoder->keyframe_period) {
    encoder->ip_period = base_encoder->keyframe_period - 1;
  }

  if (!ensure_profile_and_level (encoder))
    goto error;
  if (!ensure_bitrate (encoder))
    goto error;
  if (!ensure_sampling_desity (encoder))
    goto error;

  set_context_info (base_encoder);
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

error:
  return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
}

static gboolean
gst_vaapi_encoder_mpeg2_init (GstVaapiEncoder * base)
{
  GstVaapiEncoderMpeg2 *encoder = GST_VAAPI_ENCODER_MPEG2 (base);

  /* re-ordering */
  g_queue_init (&encoder->b_frames);
  encoder->dump_frames = FALSE;

  encoder->forward = NULL;
  encoder->backward = NULL;

  encoder->frame_num = 0;

  return TRUE;
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
gst_vaapi_encoder_mpeg2_finalize (GstVaapiEncoder * base)
{
  /*free private buffers */
  GstVaapiEncoderMpeg2 *encoder = GST_VAAPI_ENCODER_MPEG2 (base);
  GstVaapiEncPicture *pic;

  clear_references (encoder);

  while (!g_queue_is_empty (&encoder->b_frames)) {
    pic = (GstVaapiEncPicture *) g_queue_pop_head (&encoder->b_frames);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->b_frames);
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_mpeg2_set_property (GstVaapiEncoder * base_encoder,
    gint prop_id, const GValue * value)
{
  GstVaapiEncoderMpeg2 *const encoder = GST_VAAPI_ENCODER_MPEG2 (base_encoder);

  switch (prop_id) {
    case GST_VAAPI_ENCODER_MPEG2_PROP_QUANTIZER:
      encoder->cqp = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_MPEG2_PROP_MAX_BFRAMES:
      encoder->ip_period = g_value_get_uint (value);
      break;
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (MPEG2);

static inline const GstVaapiEncoderClass *
gst_vaapi_encoder_mpeg2_class (void)
{
  static const GstVaapiEncoderClass GstVaapiEncoderMpeg2Class = {
    GST_VAAPI_ENCODER_CLASS_INIT (Mpeg2, mpeg2),
    .set_property = gst_vaapi_encoder_mpeg2_set_property,
  };
  return &GstVaapiEncoderMpeg2Class;
}

GstVaapiEncoder *
gst_vaapi_encoder_mpeg2_new (GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_new (gst_vaapi_encoder_mpeg2_class (), display);
}

/**
 * gst_vaapi_encoder_mpeg2_get_default_properties:
 *
 * Determines the set of common and MPEG-2 specific encoder properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of encoder properties for #GstVaapiEncoderMpeg2,
 *   or %NULL if an error occurred.
 */
GPtrArray *
gst_vaapi_encoder_mpeg2_get_default_properties (void)
{
  const GstVaapiEncoderClass *const klass = gst_vaapi_encoder_mpeg2_class ();
  GPtrArray *props;

  props = gst_vaapi_encoder_properties_get_default (klass);
  if (!props)
    return NULL;

  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_MPEG2_PROP_QUANTIZER,
      g_param_spec_uint ("quantizer",
          "Constant Quantizer",
          "Constant quantizer (if rate-control mode is CQP)",
          GST_VAAPI_ENCODER_MPEG2_MIN_CQP, GST_VAAPI_ENCODER_MPEG2_MAX_CQP,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_MPEG2_PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes", "Max B-Frames",
          "Number of B-frames between I and P", 0,
          GST_VAAPI_ENCODER_MPEG2_MAX_MAX_BFRAMES,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  return props;
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
  unsigned int delta = -1;
  int code = 1, i;
  float frame_rate_value = seq_param->frame_rate *
      (seq_param->sequence_extension.bits.frame_rate_extension_d + 1) /
      (seq_param->sequence_extension.bits.frame_rate_extension_n + 1);

  for (i = 0; i < sizeof (frame_rate_tab) / sizeof (frame_rate_tab[0]); i++) {

    if (abs (1000 * frame_rate_tab[i].value - 1000 * frame_rate_value) < delta) {
      code = frame_rate_tab[i].code;
      delta = abs (1000 * frame_rate_tab[i].value - 1000 * frame_rate_value);
    }
  }
  return code;
}

static gboolean
gst_bit_writer_write_sps (GstBitWriter * bitwriter,
    VAEncSequenceParameterBufferMPEG2 * seq, GstVaapiEncoderMpeg2 * encoder)
{
  int frame_rate_code = find_frame_rate_code (seq);

  if (encoder->new_gop) {
    gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_SEQ, 32);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->picture_width, 12);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->picture_height, 12);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->aspect_ratio_information,
        4);
    gst_bit_writer_put_bits_uint32 (bitwriter, frame_rate_code, 4);     /* frame_rate_code */
    gst_bit_writer_put_bits_uint32 (bitwriter, (seq->bits_per_second + 399) / 400, 18); /* the low 18 bits of bit_rate */
    gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);   /* marker_bit */
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->vbv_buffer_size, 10);
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* constraint_parameter_flag, always 0 for MPEG-2 */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* load_intra_quantiser_matrix */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* load_non_intra_quantiser_matrix */

    gst_bit_writer_align_bytes (bitwriter, 0);

    gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_EXT, 32);
    gst_bit_writer_put_bits_uint32 (bitwriter, 1, 4);   /* sequence_extension id */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->sequence_extension.bits.profile_and_level_indication, 8);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->sequence_extension.bits.progressive_sequence, 1);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->sequence_extension.bits.chroma_format, 2);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->picture_width >> 12, 2);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->picture_height >> 12, 2);
    gst_bit_writer_put_bits_uint32 (bitwriter, ((seq->bits_per_second + 399) / 400) >> 18, 12); /* bit_rate_extension */
    gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);   /* marker_bit */
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->vbv_buffer_size >> 10, 8);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->sequence_extension.bits.low_delay, 1);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->sequence_extension.bits.frame_rate_extension_n, 2);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->sequence_extension.bits.frame_rate_extension_d, 5);

    gst_bit_writer_align_bytes (bitwriter, 0);

    /* gop header */
    gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_GOP, 32);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->gop_header.bits.time_code,
        25);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->gop_header.bits.closed_gop,
        1);
    gst_bit_writer_put_bits_uint32 (bitwriter, seq->gop_header.bits.broken_link,
        1);

    gst_bit_writer_align_bytes (bitwriter, 0);
  }
  return TRUE;
}

static gboolean
gst_bit_writer_write_pps (GstBitWriter * bitwriter,
    VAEncPictureParameterBufferMPEG2 * pic)
{
  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_PICUTRE, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic->temporal_reference, 10);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_type == VAEncPictureTypeIntra ? 1 :
      pic->picture_type == VAEncPictureTypePredictive ? 2 : 3, 3);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic->vbv_delay, 16);

  if (pic->picture_type == VAEncPictureTypePredictive ||
      pic->picture_type == VAEncPictureTypeBidirectional) {
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* full_pel_forward_vector, always 0 for MPEG-2 */
    gst_bit_writer_put_bits_uint32 (bitwriter, 7, 3);   /* forward_f_code, always 7 for MPEG-2 */
  }

  if (pic->picture_type == VAEncPictureTypeBidirectional) {
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);   /* full_pel_backward_vector, always 0 for MPEG-2 */
    gst_bit_writer_put_bits_uint32 (bitwriter, 7, 3);   /* backward_f_code, always 7 for MPEG-2 */
  }

  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);     /* extra_bit_picture, 0 */

  gst_bit_writer_align_bytes (bitwriter, 0);

  gst_bit_writer_put_bits_uint32 (bitwriter, START_CODE_EXT, 32);
  gst_bit_writer_put_bits_uint32 (bitwriter, 8, 4);     /* Picture Coding Extension ID: 8 */
  gst_bit_writer_put_bits_uint32 (bitwriter, pic->f_code[0][0], 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic->f_code[0][1], 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic->f_code[1][0], 4);
  gst_bit_writer_put_bits_uint32 (bitwriter, pic->f_code[1][1], 4);

  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.intra_dc_precision, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.picture_structure, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.top_field_first, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.frame_pred_frame_dct, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.concealment_motion_vectors, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.q_scale_type, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.intra_vlc_format, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.alternate_scan, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.repeat_first_field, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);     /* always chroma 420 */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.progressive_frame, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->picture_coding_extension.bits.composite_display_flag, 1);

  gst_bit_writer_align_bytes (bitwriter, 0);

  return TRUE;
}
