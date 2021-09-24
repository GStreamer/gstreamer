/*
 *  gstvaapiencoder_vp9.c - VP9 encoder
 *
 *  Copyright (C) 2016 Intel Corporation
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
#include <gst/codecparsers/gstvp9parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapiencoder_vp9.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"
#include "gstvaapiutils_vpx.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define MAX_TILE_WIDTH_B64 64

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                  \
  (GST_VAAPI_RATECONTROL_MASK (CQP) |           \
   GST_VAAPI_RATECONTROL_MASK (CBR) |           \
   GST_VAAPI_RATECONTROL_MASK (VBR))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE) |           \
   GST_VAAPI_ENCODER_TUNE_MASK (LOW_POWER))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_NONE)

#define DEFAULT_LOOP_FILTER_LEVEL 10
#define DEFAULT_SHARPNESS_LEVEL 0
#define DEFAULT_YAC_QINDEX 60

#define MAX_FRAME_WIDTH 4096
#define MAX_FRAME_HEIGHT 4096

/* Default CPB length (in milliseconds) */
#define DEFAULT_CPB_LENGTH 1500

typedef enum
{
  GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_0 = 0,
  GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_1 = 1
} GstVaapiEnoderVP9RefPicMode;

static GType
gst_vaapi_encoder_vp9_ref_pic_mode_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_0,
            "Use Keyframe(Alt & Gold) and Previousframe(Last) for prediction ",
          "mode-0"},
      {GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_1,
            "Use last three frames for prediction (n:Last n-1:Gold n-2:Alt)",
          "mode-1"},
      {0, NULL, NULL},
    };

    gtype = g_enum_register_static ("GstVaapiEncoderVP9RefPicMode", values);
  }
  return gtype;
}


/* ------------------------------------------------------------------------- */
/* --- VP9 Encoder                                                      --- */
/* ------------------------------------------------------------------------- */

struct _GstVaapiEncoderVP9
{
  GstVaapiEncoder parent_instance;
  GstVaapiProfile profile;
  guint loop_filter_level;
  guint sharpness_level;
  guint yac_qi;
  guint ref_pic_mode;
  guint frame_num;
  GstVaapiSurfaceProxy *ref_list[GST_VP9_REF_FRAMES];   /* reference list */
  guint ref_list_idx;           /* next free slot in ref_list */
  GstVaapiEntrypoint entrypoint;
  GArray *allowed_profiles;

  /* Bitrate contral parameters, CPB = Coded Picture Buffer */
  guint bitrate_bits;           /* bitrate (bits) */
  guint cpb_length;             /* length of CPB buffer (ms) */
};

/* Estimates a good enough bitrate if none was supplied */
static void
ensure_bitrate (GstVaapiEncoderVP9 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  guint bitrate;

  switch (GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    case GST_VAAPI_RATECONTROL_CBR:
    case GST_VAAPI_RATECONTROL_VBR:
      if (!base_encoder->bitrate) {
        /* FIXME: Provide better estimation */
        /* Using a 1/6 compression ratio */
        /* 12 bits per pixel fro yuv420 */
        base_encoder->bitrate =
            (GST_VAAPI_ENCODER_WIDTH (encoder) *
            GST_VAAPI_ENCODER_HEIGHT (encoder) * 12 / 6) *
            GST_VAAPI_ENCODER_FPS_N (encoder) /
            GST_VAAPI_ENCODER_FPS_D (encoder) / 1000;
        GST_INFO ("target bitrate computed to %u kbps", base_encoder->bitrate);
      }

      bitrate = (base_encoder->bitrate * 1000);
      if (bitrate != encoder->bitrate_bits) {
        GST_DEBUG ("HRD bitrate: %u bits/sec", bitrate);
        encoder->bitrate_bits = bitrate;
      }

      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }
}

static gboolean
is_profile_allowed (GstVaapiEncoderVP9 * encoder, GstVaapiProfile profile)
{
  guint i;

  if (encoder->allowed_profiles == NULL)
    return TRUE;

  for (i = 0; i < encoder->allowed_profiles->len; i++)
    if (profile ==
        g_array_index (encoder->allowed_profiles, GstVaapiProfile, i))
      return TRUE;

  return FALSE;
}

 /* Derives the profile that suits best to the configuration */
static GstVaapiEncoderStatus
ensure_profile (GstVaapiEncoderVP9 * encoder)
{
  const GstVideoFormat format =
      GST_VIDEO_INFO_FORMAT (GST_VAAPI_ENCODER_VIDEO_INFO (encoder));
  guint depth, chrome;

  if (!GST_VIDEO_FORMAT_INFO_IS_YUV (gst_video_format_get_info (format)))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  depth = GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info (format), 0);
  chrome = gst_vaapi_utils_vp9_get_chroma_format_idc
      (gst_vaapi_video_format_get_chroma_type
      (GST_VIDEO_INFO_FORMAT (GST_VAAPI_ENCODER_VIDEO_INFO (encoder))));

  encoder->profile = GST_VAAPI_PROFILE_UNKNOWN;
  /*
     Profile Color | Depth Chroma | Subsampling
     0             | 8 bit/sample | 4:2:0
     1             | 8 bit        | 4:2:2, 4:4:4
     2             | 10 or 12 bit | 4:2:0
     3             | 10 or 12 bit | 4:2:2, 4:4:4     */
  if (chrome == 3 || chrome == 2) {
    /* 4:4:4 and 4:2:2 */
    if (depth == 8) {
      encoder->profile = GST_VAAPI_PROFILE_VP9_1;
    } else if (depth == 10 || depth == 12) {
      encoder->profile = GST_VAAPI_PROFILE_VP9_3;
    }
  } else if (chrome == 1) {
    /* 4:2:0 */
    if (depth == 8) {
      encoder->profile = GST_VAAPI_PROFILE_VP9_0;
    } else if (depth == 10 || depth == 12) {
      encoder->profile = GST_VAAPI_PROFILE_VP9_2;
    }
  }

  if (encoder->profile == GST_VAAPI_PROFILE_UNKNOWN) {
    GST_WARNING ("Failed to decide VP9 profile");
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  if (!is_profile_allowed (encoder, encoder->profile)) {
    GST_WARNING ("Failed to find an allowed VP9 profile");
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  /* Ensure bitrate if not set already */
  ensure_bitrate (encoder);
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP9 *encoder = GST_VAAPI_ENCODER_VP9 (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);
  const guint DEFAULT_SURFACES_COUNT = 2;

  /* FIXME: Maximum sizes for common headers (in bytes) */

  GST_VAAPI_ENCODER_CAST (encoder)->profile = encoder->profile;

  base_encoder->num_ref_frames = 3 + DEFAULT_SURFACES_COUNT;

  /* Only YUV 4:2:0 formats are supported for now. */
  base_encoder->codedbuf_size = GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) * 3 / 2;

  base_encoder->context_info.profile = base_encoder->profile;
  base_encoder->context_info.entrypoint = encoder->entrypoint;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static gboolean
fill_sequence (GstVaapiEncoderVP9 * encoder, GstVaapiEncSequence * sequence)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  VAEncSequenceParameterBufferVP9 *const seq_param = sequence->param;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferVP9));

  seq_param->max_frame_width = MAX_FRAME_WIDTH;
  seq_param->max_frame_height = MAX_FRAME_HEIGHT;

  /* keyframe minimum interval */
  seq_param->kf_min_dist = 1;
  /* keyframe maximum interval */
  seq_param->kf_max_dist = base_encoder->keyframe_period;
  seq_param->intra_period = base_encoder->keyframe_period;
  seq_param->bits_per_second = encoder->bitrate_bits;

  return TRUE;
}

static gboolean
ensure_sequence (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (VP9, encoder);
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
ensure_control_rate_params (GstVaapiEncoderVP9 * encoder)
{
  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CQP)
    return TRUE;

  /* RateControl params */
  GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder).bits_per_second =
      encoder->bitrate_bits;
  GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder).window_size = encoder->cpb_length;

  /* *INDENT-OFF* */
  /* HRD params */
  GST_VAAPI_ENCODER_VA_HRD (encoder) = (VAEncMiscParameterHRD) {
    .buffer_size = encoder->bitrate_bits * 2,
    .initial_buffer_fullness = encoder->bitrate_bits,
  };
  /* *INDENT-ON* */

  return TRUE;
}

static gboolean
ensure_misc_params (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  if (!gst_vaapi_encoder_ensure_param_quality_level (base_encoder, picture))
    return FALSE;
  if (!gst_vaapi_encoder_ensure_param_control_rate (base_encoder, picture))
    return FALSE;
  return TRUE;
}

static void
get_ref_indices (guint ref_pic_mode, guint ref_list_idx, guint * last_idx,
    guint * gf_idx, guint * arf_idx, guint8 * refresh_frame_flags)
{
  if (ref_pic_mode == GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_0) {
    *last_idx = ref_list_idx - 1;
    *gf_idx = 1;
    *arf_idx = 2;
    *refresh_frame_flags = 0x01;
  } else if (ref_pic_mode == GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_1) {
    gint last_filled_idx = (ref_list_idx - 1) & (GST_VP9_REF_FRAMES - 1);

    *last_idx = last_filled_idx;
    *gf_idx = (last_filled_idx - 1) & (GST_VP9_REF_FRAMES - 1);
    *arf_idx = (last_filled_idx - 2) & (GST_VP9_REF_FRAMES - 1);

    *refresh_frame_flags = 1 << ((*last_idx + 1) % GST_VP9_REF_FRAMES);
  }

  GST_LOG
      ("last_ref_idx:%d gold_ref_idx:%d alt_reff_idx:%d refesh_frame_flag:%x",
      *last_idx, *gf_idx, *arf_idx, *refresh_frame_flags);
}

static gboolean
fill_picture (GstVaapiEncoderVP9 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferVP9 *const pic_param = picture->param;
  guint i, last_idx = 0, gf_idx = 0, arf_idx = 0;
  guint8 refresh_frame_flags = 0;
  gint sb_cols = 0, min_log2_tile_columns = 0;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferVP9));

  pic_param->reconstructed_frame = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->coded_buf = GST_VAAPI_CODED_BUFFER_ID (codedbuf);

  /* Update Reference Frame list */
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    memset (pic_param->reference_frames, 0xFF,
        sizeof (pic_param->reference_frames));
  else {
    for (i = 0; i < G_N_ELEMENTS (pic_param->reference_frames); i++) {
      pic_param->reference_frames[i] =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->ref_list[i]);
    }
  }

  /* It is possible to have dynamic scaling with gpu by providing
   * src and destination resoltuion. For now we are just using
   * default encoder width and height */
  pic_param->frame_width_src = GST_VAAPI_ENCODER_WIDTH (encoder);
  pic_param->frame_height_src = GST_VAAPI_ENCODER_HEIGHT (encoder);
  pic_param->frame_width_dst = GST_VAAPI_ENCODER_WIDTH (encoder);
  pic_param->frame_height_dst = GST_VAAPI_ENCODER_HEIGHT (encoder);

  pic_param->pic_flags.bits.show_frame = 1;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_P) {
    pic_param->pic_flags.bits.frame_type = GST_VP9_INTER_FRAME;

    /* use three of the reference frames (last, golden and altref)
     * for prediction */
    pic_param->ref_flags.bits.ref_frame_ctrl_l0 = 0x7;

    get_ref_indices (encoder->ref_pic_mode, encoder->ref_list_idx, &last_idx,
        &gf_idx, &arf_idx, &refresh_frame_flags);

    pic_param->ref_flags.bits.ref_last_idx = last_idx;
    pic_param->ref_flags.bits.ref_gf_idx = gf_idx;
    pic_param->ref_flags.bits.ref_arf_idx = arf_idx;
    pic_param->refresh_frame_flags = refresh_frame_flags;
  }

  /* Maximum width of a tile in units of superblocks is MAX_TILE_WIDTH_B64(64).
   * When the width is enough to partition more than MAX_TILE_WIDTH_B64(64) superblocks,
   * we need multi tiles to handle it.*/
  sb_cols = (pic_param->frame_width_src + 63) / 64;
  while ((MAX_TILE_WIDTH_B64 << min_log2_tile_columns) < sb_cols)
    ++min_log2_tile_columns;
  pic_param->log2_tile_columns = min_log2_tile_columns;

  pic_param->luma_ac_qindex = encoder->yac_qi;
  pic_param->luma_dc_qindex_delta = 1;
  pic_param->chroma_ac_qindex_delta = 1;
  pic_param->chroma_dc_qindex_delta = 1;
  pic_param->filter_level = encoder->loop_filter_level;
  pic_param->sharpness_level = encoder->sharpness_level;

  return TRUE;
}

static gboolean
ensure_picture (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_picture (encoder, picture, codedbuf, surface))
    return FALSE;

  return TRUE;
}

static void
update_ref_list (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture,
    GstVaapiSurfaceProxy * ref)
{
  guint i;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I) {
    for (i = 0; i < G_N_ELEMENTS (encoder->ref_list); i++)
      gst_vaapi_surface_proxy_replace (&encoder->ref_list[i], ref);
    gst_vaapi_surface_proxy_unref (ref);
    /* set next free slot index */
    encoder->ref_list_idx = 1;
    return;
  }

  switch (encoder->ref_pic_mode) {
    case GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_0:
      gst_vaapi_surface_proxy_replace (&encoder->ref_list[0], ref);
      gst_vaapi_surface_proxy_unref (ref);
      break;
    case GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_1:
      i = encoder->ref_list_idx;
      gst_vaapi_surface_proxy_replace (&encoder->ref_list[i], ref);
      gst_vaapi_surface_proxy_unref (ref);
      encoder->ref_list_idx = (encoder->ref_list_idx + 1) % GST_VP9_REF_FRAMES;
      break;
    default:
      g_assert ("Code shouldn't reach here");
      break;
  }
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (base_encoder);
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
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;

  update_ref_list (encoder, picture, reconstruct);

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
gst_vaapi_encoder_vp9_flush (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (base_encoder);

  encoder->frame_num = 0;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (base_encoder);
  GstVaapiEncPicture *picture = NULL;
  GstVaapiEncoderStatus status = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  if (!frame)
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

  picture = GST_VAAPI_ENC_PICTURE_NEW (VP9, encoder, frame);
  if (!picture) {
    GST_WARNING ("create VP9 picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (encoder->frame_num >= base_encoder->keyframe_period) {
    encoder->frame_num = 0;
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
gst_vaapi_encoder_vp9_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (base_encoder);
  GstVaapiEncoderStatus status;

  status = ensure_profile (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  encoder->entrypoint =
      gst_vaapi_encoder_get_entrypoint (base_encoder, encoder->profile);
  if (encoder->entrypoint == GST_VAAPI_ENTRYPOINT_INVALID) {
    GST_WARNING ("Cannot find valid profile/entrypoint pair");
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  ensure_control_rate_params (encoder);
  return set_context_info (base_encoder);
}


struct _GstVaapiEncoderVP9Class
{
  GstVaapiEncoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiEncoderVP9, gst_vaapi_encoder_vp9,
    GST_TYPE_VAAPI_ENCODER);

static void
gst_vaapi_encoder_vp9_init (GstVaapiEncoderVP9 * encoder)
{
  encoder->frame_num = 0;
  encoder->loop_filter_level = DEFAULT_LOOP_FILTER_LEVEL;
  encoder->sharpness_level = DEFAULT_SHARPNESS_LEVEL;
  encoder->yac_qi = DEFAULT_YAC_QINDEX;
  encoder->cpb_length = DEFAULT_CPB_LENGTH;
  encoder->entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;

  memset (encoder->ref_list, 0,
      G_N_ELEMENTS (encoder->ref_list) * sizeof (encoder->ref_list[0]));
  encoder->ref_list_idx = 0;

  encoder->allowed_profiles = NULL;
}

static void
gst_vaapi_encoder_vp9_finalize (GObject * object)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (object);

  if (encoder->allowed_profiles)
    g_array_unref (encoder->allowed_profiles);

  G_OBJECT_CLASS (gst_vaapi_encoder_vp9_parent_class)->finalize (object);
}

/**
 * @ENCODER_VP9_PROP_RATECONTROL: Rate control (#GstVaapiRateControl).
 * @ENCODER_VP9_PROP_TUNE: The tuning options (#GstVaapiEncoderTune).
 * @ENCODER_VP9_PROP_LOOP_FILTER_LEVEL: Loop Filter Level(uint).
 * @ENCODER_VP9_PROP_LOOP_SHARPNESS_LEVEL: Sharpness Level(uint).
 * @ENCODER_VP9_PROP_YAC_Q_INDEX: Quantization table index for luma AC
 * @ENCODER_VP9_PROP_REF_PIC_MODE: Reference picute selection modes
 * @ENCODER_VP9_PROP_CPB_LENGTH:Length of CPB buffer in milliseconds
 *
 * The set of VP9 encoder specific configurable properties.
 */
enum
{
  ENCODER_VP9_PROP_RATECONTROL = 1,
  ENCODER_VP9_PROP_TUNE,
  ENCODER_VP9_PROP_LOOP_FILTER_LEVEL,
  ENCODER_VP9_PROP_SHARPNESS_LEVEL,
  ENCODER_VP9_PROP_YAC_Q_INDEX,
  ENCODER_VP9_PROP_REF_PIC_MODE,
  ENCODER_VP9_PROP_CPB_LENGTH,
  ENCODER_VP9_N_PROPERTIES
};

static GParamSpec *properties[ENCODER_VP9_N_PROPERTIES];

static void
gst_vaapi_encoder_vp9_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (object);

  if (base_encoder->num_codedbuf_queued > 0) {
    GST_ERROR_OBJECT (object,
        "failed to set any property after encoding started");
    return;
  }

  switch (prop_id) {
    case ENCODER_VP9_PROP_RATECONTROL:
      gst_vaapi_encoder_set_rate_control (base_encoder,
          g_value_get_enum (value));
      break;
    case ENCODER_VP9_PROP_TUNE:
      gst_vaapi_encoder_set_tuning (base_encoder, g_value_get_enum (value));
      break;
    case ENCODER_VP9_PROP_LOOP_FILTER_LEVEL:
      encoder->loop_filter_level = g_value_get_uint (value);
      break;
    case ENCODER_VP9_PROP_SHARPNESS_LEVEL:
      encoder->sharpness_level = g_value_get_uint (value);
      break;
    case ENCODER_VP9_PROP_YAC_Q_INDEX:
      encoder->yac_qi = g_value_get_uint (value);
      break;
    case ENCODER_VP9_PROP_REF_PIC_MODE:
      encoder->ref_pic_mode = g_value_get_enum (value);
      break;
    case ENCODER_VP9_PROP_CPB_LENGTH:
      encoder->cpb_length = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_vaapi_encoder_vp9_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9 (object);
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);

  switch (prop_id) {
    case ENCODER_VP9_PROP_RATECONTROL:
      g_value_set_enum (value, base_encoder->rate_control);
      break;
    case ENCODER_VP9_PROP_TUNE:
      g_value_set_enum (value, base_encoder->tune);
      break;
    case ENCODER_VP9_PROP_LOOP_FILTER_LEVEL:
      g_value_set_uint (value, encoder->loop_filter_level);
      break;
    case ENCODER_VP9_PROP_SHARPNESS_LEVEL:
      g_value_set_uint (value, encoder->sharpness_level);
      break;
    case ENCODER_VP9_PROP_YAC_Q_INDEX:
      g_value_set_uint (value, encoder->yac_qi);
      break;
    case ENCODER_VP9_PROP_REF_PIC_MODE:
      g_value_set_enum (value, encoder->ref_pic_mode);
      break;
    case ENCODER_VP9_PROP_CPB_LENGTH:
      g_value_set_uint (value, encoder->cpb_length);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (VP9);

static void
gst_vaapi_encoder_vp9_class_init (GstVaapiEncoderVP9Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiEncoderClass *const encoder_class = GST_VAAPI_ENCODER_CLASS (klass);

  encoder_class->class_data = &g_class_data;
  encoder_class->reconfigure = gst_vaapi_encoder_vp9_reconfigure;
  encoder_class->reordering = gst_vaapi_encoder_vp9_reordering;
  encoder_class->encode = gst_vaapi_encoder_vp9_encode;
  encoder_class->flush = gst_vaapi_encoder_vp9_flush;

  object_class->set_property = gst_vaapi_encoder_vp9_set_property;
  object_class->get_property = gst_vaapi_encoder_vp9_get_property;
  object_class->finalize = gst_vaapi_encoder_vp9_finalize;

  properties[ENCODER_VP9_PROP_RATECONTROL] =
      g_param_spec_enum ("rate-control",
      "Rate Control", "Rate control mode",
      g_class_data.rate_control_get_type (),
      g_class_data.default_rate_control,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP9_PROP_TUNE] =
      g_param_spec_enum ("tune",
      "Encoder Tuning",
      "Encoder tuning option",
      g_class_data.encoder_tune_get_type (),
      g_class_data.default_encoder_tune,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP9_PROP_LOOP_FILTER_LEVEL] =
      g_param_spec_uint ("loop-filter-level",
      "Loop Filter Level",
      "Controls the deblocking filter strength",
      0, 63, DEFAULT_LOOP_FILTER_LEVEL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP9_PROP_SHARPNESS_LEVEL] =
      g_param_spec_uint ("sharpness-level",
      "Sharpness Level",
      "Controls the deblocking filter sensitivity",
      0, 7, DEFAULT_SHARPNESS_LEVEL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP9_PROP_YAC_Q_INDEX] =
      g_param_spec_uint ("yac-qi",
      "Luma AC Quant Table index",
      "Quantization Table index for Luma AC Coefficients",
      0, 255, DEFAULT_YAC_QINDEX,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_VP9_PROP_REF_PIC_MODE] =
      g_param_spec_enum ("ref-pic-mode",
      "RefPic Selection",
      "Reference Picture Selection Modes",
      gst_vaapi_encoder_vp9_ref_pic_mode_type (),
      GST_VAAPI_ENCODER_VP9_REF_PIC_MODE_0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVaapiEncoderVP9:cpb-length:
   *
   * The size of the Coded Picture Buffer , which means
   * the window size in milliseconds.
   *
   */
  properties[ENCODER_VP9_PROP_CPB_LENGTH] =
      g_param_spec_uint ("cpb-length",
      "CPB Length", "Length of the CPB_buffer/window_size in milliseconds",
      1, 10000, DEFAULT_CPB_LENGTH,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  g_object_class_install_properties (object_class, ENCODER_VP9_N_PROPERTIES,
      properties);

  gst_type_mark_as_plugin_api (g_class_data.rate_control_get_type (), 0);
  gst_type_mark_as_plugin_api (g_class_data.encoder_tune_get_type (), 0);
}

/**
 * gst_vaapi_encoder_vp9_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for VP9 encoding.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_vp9_new (GstVaapiDisplay * display)
{
  return g_object_new (GST_TYPE_VAAPI_ENCODER_VP9, "display", display, NULL);
}

/**
 * gst_vaapi_encoder_vp9_set_allowed_profiles:
 * @encoder: a #GstVaapiEncoderVP9
 * @profiles: a #GArray of all allowed #GstVaapiProfile.
 *
 * Set the all allowed profiles for the encoder.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_encoder_vp9_set_allowed_profiles (GstVaapiEncoderVP9 * encoder,
    GArray * profiles)
{
  g_return_val_if_fail (profiles != 0, FALSE);

  encoder->allowed_profiles = g_array_ref (profiles);
  return TRUE;
}
