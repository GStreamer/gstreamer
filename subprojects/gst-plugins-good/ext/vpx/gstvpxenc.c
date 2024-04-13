/* VPX
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010-2012 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(HAVE_VP8_ENCODER) || defined(HAVE_VP9_ENCODER)

/* glib decided in 2.32 it would be a great idea to deprecated GValueArray without
 * providing an alternative
 *
 * See https://bugzilla.gnome.org/show_bug.cgi?id=667228
 * */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/tag/tag.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstvp8utils.h"
#include "gstvpxenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_vpxenc_debug);
#define GST_CAT_DEFAULT gst_vpxenc_debug

/* From vp8/vp8_cx_iface.c and vp9/vp9_cx_iface.c */
#define DEFAULT_PROFILE 0

#define DEFAULT_RC_END_USAGE VPX_VBR
#define DEFAULT_RC_TARGET_BITRATE 0
#define DEFAULT_RC_MIN_QUANTIZER 4
#define DEFAULT_RC_MAX_QUANTIZER 63

#define DEFAULT_RC_DROPFRAME_THRESH 0
#define DEFAULT_RC_RESIZE_ALLOWED 0
#define DEFAULT_RC_RESIZE_UP_THRESH 30
#define DEFAULT_RC_RESIZE_DOWN_THRESH 60
#define DEFAULT_RC_UNDERSHOOT_PCT 100
#define DEFAULT_RC_OVERSHOOT_PCT 100
#define DEFAULT_RC_BUF_SZ 6000
#define DEFAULT_RC_BUF_INITIAL_SZ 4000
#define DEFAULT_RC_BUF_OPTIMAL_SZ 5000
#define DEFAULT_RC_2PASS_VBR_BIAS_PCT 50
#define DEFAULT_RC_2PASS_VBR_MINSECTION_PCT 0
#define DEFAULT_RC_2PASS_VBR_MAXSECTION_PCT 400

#define DEFAULT_KF_MODE VPX_KF_AUTO
#define DEFAULT_KF_MAX_DIST 128

#define DEFAULT_MULTIPASS_MODE VPX_RC_ONE_PASS
#define DEFAULT_MULTIPASS_CACHE_FILE "multipass.cache"

#define DEFAULT_TS_NUMBER_LAYERS 1
#define DEFAULT_TS_TARGET_BITRATE NULL
#define DEFAULT_TS_RATE_DECIMATOR NULL
#define DEFAULT_TS_PERIODICITY 0
#define DEFAULT_TS_LAYER_ID NULL
#define DEFAULT_TS_LAYER_FLAGS NULL
#define DEFAULT_TS_LAYER_SYNC_FLAGS NULL

#define DEFAULT_ERROR_RESILIENT 0
#define DEFAULT_LAG_IN_FRAMES 0

#define DEFAULT_THREADS 0

#define DEFAULT_H_SCALING_MODE VP8E_NORMAL
#define DEFAULT_V_SCALING_MODE VP8E_NORMAL
#define DEFAULT_CPU_USED 0
#define DEFAULT_ENABLE_AUTO_ALT_REF FALSE
#define DEFAULT_DEADLINE VPX_DL_GOOD_QUALITY
#define DEFAULT_NOISE_SENSITIVITY 0
#define DEFAULT_SHARPNESS 0

/* Use same default value as Chromium/webrtc. */
#define DEFAULT_STATIC_THRESHOLD 1

#define DEFAULT_TOKEN_PARTITIONS 0
#define DEFAULT_ARNR_MAXFRAMES 0
#define DEFAULT_ARNR_STRENGTH 3
#define DEFAULT_ARNR_TYPE 3
#define DEFAULT_TUNING VP8_TUNE_PSNR
#define DEFAULT_CQ_LEVEL 10
#define DEFAULT_MAX_INTRA_BITRATE_PCT 0
#define DEFAULT_TIMEBASE_N 0
#define DEFAULT_TIMEBASE_D 1

#define DEFAULT_BITS_PER_PIXEL 0.0434

enum
{
  PROP_0,
  PROP_RC_END_USAGE,
  PROP_RC_TARGET_BITRATE,
  PROP_RC_MIN_QUANTIZER,
  PROP_RC_MAX_QUANTIZER,
  PROP_RC_DROPFRAME_THRESH,
  PROP_RC_RESIZE_ALLOWED,
  PROP_RC_RESIZE_UP_THRESH,
  PROP_RC_RESIZE_DOWN_THRESH,
  PROP_RC_UNDERSHOOT_PCT,
  PROP_RC_OVERSHOOT_PCT,
  PROP_RC_BUF_SZ,
  PROP_RC_BUF_INITIAL_SZ,
  PROP_RC_BUF_OPTIMAL_SZ,
  PROP_RC_2PASS_VBR_BIAS_PCT,
  PROP_RC_2PASS_VBR_MINSECTION_PCT,
  PROP_RC_2PASS_VBR_MAXSECTION_PCT,
  PROP_KF_MODE,
  PROP_KF_MAX_DIST,
  PROP_TS_NUMBER_LAYERS,
  PROP_TS_TARGET_BITRATE,
  PROP_TS_RATE_DECIMATOR,
  PROP_TS_PERIODICITY,
  PROP_TS_LAYER_ID,
  PROP_TS_LAYER_FLAGS,
  PROP_TS_LAYER_SYNC_FLAGS,
  PROP_MULTIPASS_MODE,
  PROP_MULTIPASS_CACHE_FILE,
  PROP_ERROR_RESILIENT,
  PROP_LAG_IN_FRAMES,
  PROP_THREADS,
  PROP_DEADLINE,
  PROP_H_SCALING_MODE,
  PROP_V_SCALING_MODE,
  PROP_CPU_USED,
  PROP_ENABLE_AUTO_ALT_REF,
  PROP_NOISE_SENSITIVITY,
  PROP_SHARPNESS,
  PROP_STATIC_THRESHOLD,
  PROP_TOKEN_PARTITIONS,
  PROP_ARNR_MAXFRAMES,
  PROP_ARNR_STRENGTH,
  PROP_ARNR_TYPE,
  PROP_TUNING,
  PROP_CQ_LEVEL,
  PROP_MAX_INTRA_BITRATE_PCT,
  PROP_TIMEBASE,
  PROP_BITS_PER_PIXEL
};


#define GST_VPX_ENC_END_USAGE_TYPE (gst_vpx_enc_end_usage_get_type())
static GType
gst_vpx_enc_end_usage_get_type (void)
{
  static const GEnumValue values[] = {
    {VPX_VBR, "Variable Bit Rate (VBR) mode", "vbr"},
    {VPX_CBR, "Constant Bit Rate (CBR) mode", "cbr"},
    {VPX_CQ, "Constant Quality Mode (CQ) mode", "cq"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVPXEncEndUsage", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_MULTIPASS_MODE_TYPE (gst_vpx_enc_multipass_mode_get_type())
static GType
gst_vpx_enc_multipass_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {VPX_RC_ONE_PASS, "One pass encoding (default)", "one-pass"},
    {VPX_RC_FIRST_PASS, "First pass of multipass encoding", "first-pass"},
    {VPX_RC_LAST_PASS, "Last pass of multipass encoding", "last-pass"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVPXEncMultipassMode", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_KF_MODE_TYPE (gst_vpx_enc_kf_mode_get_type())
static GType
gst_vpx_enc_kf_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {VPX_KF_AUTO, "Determine optimal placement automatically", "auto"},
    {VPX_KF_DISABLED, "Don't automatically place keyframes", "disabled"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVPXEncKfMode", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_TUNING_TYPE (gst_vpx_enc_tuning_get_type())
static GType
gst_vpx_enc_tuning_get_type (void)
{
  static const GEnumValue values[] = {
    {VP8_TUNE_PSNR, "Tune for PSNR", "psnr"},
    {VP8_TUNE_SSIM, "Tune for SSIM", "ssim"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVPXEncTuning", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_SCALING_MODE_TYPE (gst_vpx_enc_scaling_mode_get_type())
static GType
gst_vpx_enc_scaling_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {VP8E_NORMAL, "Normal", "normal"},
    {VP8E_FOURFIVE, "4:5", "4:5"},
    {VP8E_THREEFIVE, "3:5", "3:5"},
    {VP8E_ONETWO, "1:2", "1:2"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVPXEncScalingMode", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_TOKEN_PARTITIONS_TYPE (gst_vpx_enc_token_partitions_get_type())
static GType
gst_vpx_enc_token_partitions_get_type (void)
{
  static const GEnumValue values[] = {
    {VP8_ONE_TOKENPARTITION, "One token partition", "1"},
    {VP8_TWO_TOKENPARTITION, "Two token partitions", "2"},
    {VP8_FOUR_TOKENPARTITION, "Four token partitions", "4"},
    {VP8_EIGHT_TOKENPARTITION, "Eight token partitions", "8"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVPXEncTokenPartitions", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_ER_FLAGS_TYPE (gst_vpx_enc_er_flags_get_type())
static GType
gst_vpx_enc_er_flags_get_type (void)
{
  static const GFlagsValue values[] = {
    {VPX_ERROR_RESILIENT_DEFAULT, "Default error resilience", "default"},
    {VPX_ERROR_RESILIENT_PARTITIONS,
        "Allow partitions to be decoded independently", "partitions"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_flags_register_static ("GstVPXEncErFlags", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VPX_ENC_TS_LAYER_FLAGS_TYPE (gst_vpx_enc_ts_layer_flags_get_type())
static GType
gst_vpx_enc_ts_layer_flags_get_type (void)
{
  static const GFlagsValue values[] = {
    {VP8_EFLAG_NO_REF_LAST, "Don't reference the last frame", "no-ref-last"},
    {VP8_EFLAG_NO_REF_GF, "Don't reference the golden frame", "no-ref-golden"},
    {VP8_EFLAG_NO_REF_ARF, "Don't reference the alternate reference frame",
        "no-ref-alt"},
    {VP8_EFLAG_NO_UPD_LAST, "Don't update the last frame", "no-upd-last"},
    {VP8_EFLAG_NO_UPD_GF, "Don't update the golden frame", "no-upd-golden"},
    {VP8_EFLAG_NO_UPD_ARF, "Don't update the alternate reference frame",
        "no-upd-alt"},
    {VP8_EFLAG_NO_UPD_ENTROPY, "Disable entropy update", "no-upd-entropy"},
    {0, NULL, NULL}
  };
  static GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_flags_register_static ("GstVPXEncTsLayerFlags", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

static void gst_vpx_enc_finalize (GObject * object);
static void gst_vpx_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vpx_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_vpx_enc_start (GstVideoEncoder * encoder);
static gboolean gst_vpx_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_vpx_enc_set_format (GstVideoEncoder *
    video_encoder, GstVideoCodecState * state);
static GstFlowReturn gst_vpx_enc_finish (GstVideoEncoder * video_encoder);
static gboolean gst_vpx_enc_flush (GstVideoEncoder * video_encoder);
static GstFlowReturn gst_vpx_enc_drain (GstVideoEncoder * video_encoder);
static GstFlowReturn gst_vpx_enc_handle_frame (GstVideoEncoder *
    video_encoder, GstVideoCodecFrame * frame);
static gboolean gst_vpx_enc_sink_event (GstVideoEncoder *
    video_encoder, GstEvent * event);
static gboolean gst_vpx_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_vpx_enc_transform_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, GstMeta * meta);

#define parent_class gst_vpx_enc_parent_class
G_DEFINE_TYPE_WITH_CODE (GstVPXEnc, gst_vpx_enc, GST_TYPE_VIDEO_ENCODER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL);
    G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL););

static void
gst_vpx_enc_class_init (GstVPXEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *video_encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_vpx_enc_set_property;
  gobject_class->get_property = gst_vpx_enc_get_property;
  gobject_class->finalize = gst_vpx_enc_finalize;

  video_encoder_class->start = gst_vpx_enc_start;
  video_encoder_class->stop = gst_vpx_enc_stop;
  video_encoder_class->handle_frame = gst_vpx_enc_handle_frame;
  video_encoder_class->set_format = gst_vpx_enc_set_format;
  video_encoder_class->flush = gst_vpx_enc_flush;
  video_encoder_class->finish = gst_vpx_enc_finish;
  video_encoder_class->sink_event = gst_vpx_enc_sink_event;
  video_encoder_class->propose_allocation = gst_vpx_enc_propose_allocation;
  video_encoder_class->transform_meta = gst_vpx_enc_transform_meta;

  g_object_class_install_property (gobject_class, PROP_RC_END_USAGE,
      g_param_spec_enum ("end-usage", "Rate control mode",
          "Rate control mode",
          GST_VPX_ENC_END_USAGE_TYPE, DEFAULT_RC_END_USAGE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate", "Target bitrate",
          "Target bitrate (in bits/sec) (0: auto - bitrate depends on "
          "resolution, see \"bits-per-pixel\" property for more info)",
          0, G_MAXINT, DEFAULT_RC_TARGET_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_MIN_QUANTIZER,
      g_param_spec_int ("min-quantizer", "Minimum Quantizer",
          "Minimum Quantizer (best)",
          0, 63, DEFAULT_RC_MIN_QUANTIZER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_MAX_QUANTIZER,
      g_param_spec_int ("max-quantizer", "Maximum Quantizer",
          "Maximum Quantizer (worst)",
          0, 63, DEFAULT_RC_MAX_QUANTIZER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_DROPFRAME_THRESH,
      g_param_spec_int ("dropframe-threshold", "Drop Frame Threshold",
          "Temporal resampling threshold (buf %)",
          0, 100, DEFAULT_RC_DROPFRAME_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_RESIZE_ALLOWED,
      g_param_spec_boolean ("resize-allowed", "Resize Allowed",
          "Allow spatial resampling",
          DEFAULT_RC_RESIZE_ALLOWED,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_RESIZE_UP_THRESH,
      g_param_spec_int ("resize-up-threshold", "Resize Up Threshold",
          "Upscale threshold (buf %)",
          0, 100, DEFAULT_RC_RESIZE_UP_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_RESIZE_DOWN_THRESH,
      g_param_spec_int ("resize-down-threshold", "Resize Down Threshold",
          "Downscale threshold (buf %)",
          0, 100, DEFAULT_RC_RESIZE_DOWN_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_UNDERSHOOT_PCT,
      g_param_spec_int ("undershoot", "Undershoot PCT",
          "Datarate undershoot (min) target (%)",
          0, 1000, DEFAULT_RC_UNDERSHOOT_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_OVERSHOOT_PCT,
      g_param_spec_int ("overshoot", "Overshoot PCT",
          "Datarate overshoot (max) target (%)",
          0, 1000, DEFAULT_RC_OVERSHOOT_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_BUF_SZ,
      g_param_spec_int ("buffer-size", "Buffer size",
          "Client buffer size (ms)",
          0, G_MAXINT, DEFAULT_RC_BUF_SZ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_BUF_INITIAL_SZ,
      g_param_spec_int ("buffer-initial-size", "Buffer initial size",
          "Initial client buffer size (ms)",
          0, G_MAXINT, DEFAULT_RC_BUF_INITIAL_SZ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_BUF_OPTIMAL_SZ,
      g_param_spec_int ("buffer-optimal-size", "Buffer optimal size",
          "Optimal client buffer size (ms)",
          0, G_MAXINT, DEFAULT_RC_BUF_OPTIMAL_SZ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_RC_2PASS_VBR_BIAS_PCT,
      g_param_spec_int ("twopass-vbr-bias", "2-pass VBR bias",
          "CBR/VBR bias (0=CBR, 100=VBR)",
          0, 100, DEFAULT_RC_2PASS_VBR_BIAS_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class,
      PROP_RC_2PASS_VBR_MINSECTION_PCT,
      g_param_spec_int ("twopass-vbr-minsection", "2-pass GOP min bitrate",
          "GOP minimum bitrate (% target)", 0, G_MAXINT,
          DEFAULT_RC_2PASS_VBR_MINSECTION_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class,
      PROP_RC_2PASS_VBR_MAXSECTION_PCT,
      g_param_spec_int ("twopass-vbr-maxsection", "2-pass GOP max bitrate",
          "GOP maximum bitrate (% target)", 0, G_MAXINT,
          DEFAULT_RC_2PASS_VBR_MINSECTION_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_KF_MODE,
      g_param_spec_enum ("keyframe-mode", "Keyframe Mode",
          "Keyframe placement",
          GST_VPX_ENC_KF_MODE_TYPE, DEFAULT_KF_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_KF_MAX_DIST,
      g_param_spec_int ("keyframe-max-dist", "Keyframe max distance",
          "Maximum distance between keyframes (number of frames)",
          0, G_MAXINT, DEFAULT_KF_MAX_DIST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_MULTIPASS_MODE,
      g_param_spec_enum ("multipass-mode", "Multipass Mode",
          "Multipass encode mode",
          GST_VPX_ENC_MULTIPASS_MODE_TYPE, DEFAULT_MULTIPASS_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_MULTIPASS_CACHE_FILE,
      g_param_spec_string ("multipass-cache-file", "Multipass Cache File",
          "Multipass cache file. "
          "If stream caps reinited, multiple files will be created: "
          "file, file.1, file.2, ... and so on.",
          DEFAULT_MULTIPASS_CACHE_FILE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TS_NUMBER_LAYERS,
      g_param_spec_int ("temporal-scalability-number-layers",
          "Number of coding layers", "Number of coding layers to use", 1, 5,
          DEFAULT_TS_NUMBER_LAYERS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_TS_TARGET_BITRATE,
      g_param_spec_value_array ("temporal-scalability-target-bitrate",
          "Coding layer target bitrates",
          "Target bitrates (bits/sec) for coding layers (one per layer)",
          g_param_spec_int ("target-bitrate", "Target bitrate",
              "Target bitrate", 0, G_MAXINT, DEFAULT_RC_TARGET_BITRATE,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_DOC_SHOW_DEFAULT));

  g_object_class_install_property (gobject_class, PROP_TS_RATE_DECIMATOR,
      g_param_spec_value_array ("temporal-scalability-rate-decimator",
          "Coding layer rate decimator",
          "Rate decimation factors for each layer",
          g_param_spec_int ("rate-decimator", "Rate decimator",
              "Rate decimator", 0, 1000000000, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_DOC_SHOW_DEFAULT));

  g_object_class_install_property (gobject_class, PROP_TS_PERIODICITY,
      g_param_spec_int ("temporal-scalability-periodicity",
          "Coding layer periodicity",
          "Length of sequence that defines layer membership periodicity", 0, 16,
          DEFAULT_TS_PERIODICITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_TS_LAYER_ID,
      g_param_spec_value_array ("temporal-scalability-layer-id",
          "Coding layer identification",
          "Sequence defining coding layer membership",
          g_param_spec_int ("layer-id", "Layer ID", "Layer ID", 0, 4, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_DOC_SHOW_DEFAULT));

  /**
   * GstVPXEnc:temporal-scalability-layer-flags:
   *
   * Sequence defining coding layer flags
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_TS_LAYER_FLAGS,
      gst_param_spec_array ("temporal-scalability-layer-flags",
          "Coding layer flags", "Sequence defining coding layer flags",
          g_param_spec_flags ("flags", "Flags", "Flags",
              GST_VPX_ENC_TS_LAYER_FLAGS_TYPE, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVPXEnc:temporal-scalability-layer-sync-flags:
   *
   * Sequence defining coding layer sync flags
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_TS_LAYER_SYNC_FLAGS,
      gst_param_spec_array ("temporal-scalability-layer-sync-flags",
          "Coding layer sync flags",
          "Sequence defining coding layer sync flags",
          g_param_spec_boolean ("flags", "Flags", "Flags", FALSE,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LAG_IN_FRAMES,
      g_param_spec_int ("lag-in-frames", "Lag in frames",
          "Maximum number of frames to lag",
          0, 25, DEFAULT_LAG_IN_FRAMES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_ERROR_RESILIENT,
      g_param_spec_flags ("error-resilient", "Error resilient",
          "Error resilience flags",
          GST_VPX_ENC_ER_FLAGS_TYPE, DEFAULT_ERROR_RESILIENT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_THREADS,
      g_param_spec_int ("threads", "Threads",
          "Number of threads to use",
          0, 64, DEFAULT_THREADS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_DEADLINE,
      g_param_spec_int64 ("deadline", "Deadline",
          "Deadline per frame (usec, 0=best, 1=realtime)",
          0, G_MAXINT64, DEFAULT_DEADLINE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_H_SCALING_MODE,
      g_param_spec_enum ("horizontal-scaling-mode", "Horizontal scaling mode",
          "Horizontal scaling mode",
          GST_VPX_ENC_SCALING_MODE_TYPE, DEFAULT_H_SCALING_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_V_SCALING_MODE,
      g_param_spec_enum ("vertical-scaling-mode", "Vertical scaling mode",
          "Vertical scaling mode",
          GST_VPX_ENC_SCALING_MODE_TYPE, DEFAULT_V_SCALING_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_CPU_USED,
      g_param_spec_int ("cpu-used", "CPU used",
          "CPU used",
          -16, 16, DEFAULT_CPU_USED,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_ENABLE_AUTO_ALT_REF,
      g_param_spec_boolean ("auto-alt-ref", "Auto alt reference frames",
          "Automatically generate AltRef frames",
          DEFAULT_ENABLE_AUTO_ALT_REF,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_NOISE_SENSITIVITY,
      g_param_spec_int ("noise-sensitivity", "Noise sensitivity",
          "Noise sensisivity (frames to blur)",
          0, 6, DEFAULT_NOISE_SENSITIVITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_SHARPNESS,
      g_param_spec_int ("sharpness", "Sharpness",
          "Filter sharpness",
          0, 7, DEFAULT_SHARPNESS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_STATIC_THRESHOLD,
      g_param_spec_int ("static-threshold", "Static Threshold",
          "Motion detection threshold. Recommendation is to set 100 for "
          "screen/window sharing", 0, G_MAXINT, DEFAULT_STATIC_THRESHOLD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_TOKEN_PARTITIONS,
      g_param_spec_enum ("token-partitions", "Token partitions",
          "Number of token partitions",
          GST_VPX_ENC_TOKEN_PARTITIONS_TYPE, DEFAULT_TOKEN_PARTITIONS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_ARNR_MAXFRAMES,
      g_param_spec_int ("arnr-maxframes", "AltRef max frames",
          "AltRef maximum number of frames",
          0, 15, DEFAULT_ARNR_MAXFRAMES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_ARNR_STRENGTH,
      g_param_spec_int ("arnr-strength", "AltRef strength",
          "AltRef strength",
          0, 6, DEFAULT_ARNR_STRENGTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_ARNR_TYPE,
      g_param_spec_int ("arnr-type", "AltRef type",
          "AltRef type",
          1, 3, DEFAULT_ARNR_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_DEPRECATED | GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_TUNING,
      g_param_spec_enum ("tuning", "Tuning",
          "Tuning",
          GST_VPX_ENC_TUNING_TYPE, DEFAULT_TUNING,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_CQ_LEVEL,
      g_param_spec_int ("cq-level", "Constrained quality level",
          "Constrained quality level",
          0, 63, DEFAULT_CQ_LEVEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_MAX_INTRA_BITRATE_PCT,
      g_param_spec_int ("max-intra-bitrate", "Max Intra bitrate",
          "Maximum Intra frame bitrate",
          0, G_MAXINT, DEFAULT_MAX_INTRA_BITRATE_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  g_object_class_install_property (gobject_class, PROP_TIMEBASE,
      gst_param_spec_fraction ("timebase", "Shortest interframe time",
          "Fraction of one second that is the shortest interframe time - normally left as zero which will default to the framerate",
          0, 1, G_MAXINT, 1, DEFAULT_TIMEBASE_N, DEFAULT_TIMEBASE_D,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_DOC_SHOW_DEFAULT));

  g_object_class_install_property (gobject_class, PROP_BITS_PER_PIXEL,
      g_param_spec_float ("bits-per-pixel", "Bits per pixel",
          "Factor to convert number of pixels to bitrate value "
          "(only has an effect if target-bitrate=0)",
          0.0, G_MAXFLOAT, DEFAULT_BITS_PER_PIXEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_DOC_SHOW_DEFAULT)));

  GST_DEBUG_CATEGORY_INIT (gst_vpxenc_debug, "vpxenc", 0, "VPX Encoder");

  gst_type_mark_as_plugin_api (GST_VPX_ENC_END_USAGE_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VPX_ENC_MULTIPASS_MODE_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VPX_ENC_KF_MODE_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VPX_ENC_TUNING_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VPX_ENC_SCALING_MODE_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VPX_ENC_TOKEN_PARTITIONS_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VPX_ENC_ER_FLAGS_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_VPX_ENC, 0);
}

static void
gst_vpx_enc_init (GstVPXEnc * gst_vpx_enc)
{
  GST_DEBUG_OBJECT (gst_vpx_enc, "init");
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (gst_vpx_enc));

  gst_vpx_enc->cfg.rc_end_usage = DEFAULT_RC_END_USAGE;
  gst_vpx_enc->cfg.rc_target_bitrate = DEFAULT_RC_TARGET_BITRATE / 1000;
  gst_vpx_enc->rc_target_bitrate_auto = DEFAULT_RC_TARGET_BITRATE == 0;
  gst_vpx_enc->cfg.rc_min_quantizer = DEFAULT_RC_MIN_QUANTIZER;
  gst_vpx_enc->cfg.rc_max_quantizer = DEFAULT_RC_MAX_QUANTIZER;
  gst_vpx_enc->cfg.rc_dropframe_thresh = DEFAULT_RC_DROPFRAME_THRESH;
  gst_vpx_enc->cfg.rc_resize_allowed = DEFAULT_RC_RESIZE_ALLOWED;
  gst_vpx_enc->cfg.rc_resize_up_thresh = DEFAULT_RC_RESIZE_UP_THRESH;
  gst_vpx_enc->cfg.rc_resize_down_thresh = DEFAULT_RC_RESIZE_DOWN_THRESH;
  gst_vpx_enc->cfg.rc_undershoot_pct = DEFAULT_RC_UNDERSHOOT_PCT;
  gst_vpx_enc->cfg.rc_overshoot_pct = DEFAULT_RC_OVERSHOOT_PCT;
  gst_vpx_enc->cfg.rc_buf_sz = DEFAULT_RC_BUF_SZ;
  gst_vpx_enc->cfg.rc_buf_initial_sz = DEFAULT_RC_BUF_INITIAL_SZ;
  gst_vpx_enc->cfg.rc_buf_optimal_sz = DEFAULT_RC_BUF_OPTIMAL_SZ;
  gst_vpx_enc->cfg.rc_2pass_vbr_bias_pct = DEFAULT_RC_2PASS_VBR_BIAS_PCT;
  gst_vpx_enc->cfg.rc_2pass_vbr_minsection_pct =
      DEFAULT_RC_2PASS_VBR_MINSECTION_PCT;
  gst_vpx_enc->cfg.rc_2pass_vbr_maxsection_pct =
      DEFAULT_RC_2PASS_VBR_MAXSECTION_PCT;
  gst_vpx_enc->cfg.kf_mode = DEFAULT_KF_MODE;
  gst_vpx_enc->cfg.kf_max_dist = DEFAULT_KF_MAX_DIST;
  gst_vpx_enc->cfg.g_pass = DEFAULT_MULTIPASS_MODE;
  gst_vpx_enc->multipass_cache_prefix = g_strdup (DEFAULT_MULTIPASS_CACHE_FILE);
  gst_vpx_enc->multipass_cache_file = NULL;
  gst_vpx_enc->multipass_cache_idx = 0;
  gst_vpx_enc->cfg.ts_number_layers = DEFAULT_TS_NUMBER_LAYERS;
  gst_vpx_enc->n_ts_target_bitrate = 0;
  gst_vpx_enc->n_ts_rate_decimator = 0;
  gst_vpx_enc->cfg.ts_periodicity = DEFAULT_TS_PERIODICITY;
  gst_vpx_enc->n_ts_layer_id = 0;
  gst_vpx_enc->n_ts_layer_flags = 0;
  gst_vpx_enc->ts_layer_flags = NULL;
  gst_vpx_enc->n_ts_layer_sync_flags = 0;
  gst_vpx_enc->ts_layer_sync_flags = NULL;
  gst_vpx_enc->cfg.g_error_resilient = DEFAULT_ERROR_RESILIENT;
  gst_vpx_enc->cfg.g_lag_in_frames = DEFAULT_LAG_IN_FRAMES;
  gst_vpx_enc->cfg.g_threads = DEFAULT_THREADS;
  gst_vpx_enc->deadline = DEFAULT_DEADLINE;
  gst_vpx_enc->h_scaling_mode = DEFAULT_H_SCALING_MODE;
  gst_vpx_enc->v_scaling_mode = DEFAULT_V_SCALING_MODE;
  gst_vpx_enc->cpu_used = DEFAULT_CPU_USED;
  gst_vpx_enc->enable_auto_alt_ref = DEFAULT_ENABLE_AUTO_ALT_REF;
  gst_vpx_enc->noise_sensitivity = DEFAULT_NOISE_SENSITIVITY;
  gst_vpx_enc->sharpness = DEFAULT_SHARPNESS;
  gst_vpx_enc->static_threshold = DEFAULT_STATIC_THRESHOLD;
  gst_vpx_enc->token_partitions = DEFAULT_TOKEN_PARTITIONS;
  gst_vpx_enc->arnr_maxframes = DEFAULT_ARNR_MAXFRAMES;
  gst_vpx_enc->arnr_strength = DEFAULT_ARNR_STRENGTH;
  gst_vpx_enc->arnr_type = DEFAULT_ARNR_TYPE;
  gst_vpx_enc->tuning = DEFAULT_TUNING;
  gst_vpx_enc->cq_level = DEFAULT_CQ_LEVEL;
  gst_vpx_enc->max_intra_bitrate_pct = DEFAULT_MAX_INTRA_BITRATE_PCT;
  gst_vpx_enc->timebase_n = DEFAULT_TIMEBASE_N;
  gst_vpx_enc->timebase_d = DEFAULT_TIMEBASE_D;
  gst_vpx_enc->bits_per_pixel = DEFAULT_BITS_PER_PIXEL;
  gst_vpx_enc->tl0picidx = 0;
  gst_vpx_enc->prev_was_keyframe = FALSE;

  gst_vpx_enc->cfg.g_profile = DEFAULT_PROFILE;

  g_mutex_init (&gst_vpx_enc->encoder_lock);
}

static void
gst_vpx_enc_finalize (GObject * object)
{
  GstVPXEnc *gst_vpx_enc;

  GST_DEBUG_OBJECT (object, "finalize");

  g_return_if_fail (GST_IS_VPX_ENC (object));
  gst_vpx_enc = GST_VPX_ENC (object);

  g_free (gst_vpx_enc->ts_layer_flags);
  g_free (gst_vpx_enc->ts_layer_sync_flags);

  g_free (gst_vpx_enc->multipass_cache_prefix);
  g_free (gst_vpx_enc->multipass_cache_file);
  gst_vpx_enc->multipass_cache_idx = 0;


  if (gst_vpx_enc->input_state)
    gst_video_codec_state_unref (gst_vpx_enc->input_state);

  g_mutex_clear (&gst_vpx_enc->encoder_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vpx_enc_set_auto_bitrate (GstVPXEnc * encoder)
{
  if (encoder->input_state != NULL) {
    guint size;
    guint pixels_per_sec;
    guint target_bitrate;
    guint fps_n, fps_d;

    if (GST_VIDEO_INFO_FPS_N (&encoder->input_state->info) != 0) {
      fps_n = GST_VIDEO_INFO_FPS_N (&encoder->input_state->info);
      fps_d = GST_VIDEO_INFO_FPS_D (&encoder->input_state->info);
    } else {
      /* otherwise assume 30 frames per second as a fallback */
      fps_n = 30;
      fps_d = 1;
    }

    size =
        GST_VIDEO_INFO_WIDTH (&encoder->input_state->info) *
        GST_VIDEO_INFO_HEIGHT (&encoder->input_state->info);
    pixels_per_sec = size * fps_n / fps_d;
    target_bitrate = pixels_per_sec * encoder->bits_per_pixel;

    GST_DEBUG_OBJECT (encoder,
        "Setting autobitrate for %ux%ux @ %u/%ufps %.4f = %ubps",
        GST_VIDEO_INFO_WIDTH (&encoder->input_state->info),
        GST_VIDEO_INFO_HEIGHT (&encoder->input_state->info),
        GST_VIDEO_INFO_FPS_N (&encoder->input_state->info),
        GST_VIDEO_INFO_FPS_D (&encoder->input_state->info),
        encoder->bits_per_pixel, target_bitrate);

    encoder->cfg.rc_target_bitrate = target_bitrate / 1000;
  }
}

static void
gst_vpx_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVPXEnc *gst_vpx_enc;
  gboolean global = FALSE;
  vpx_codec_err_t status;

  g_return_if_fail (GST_IS_VPX_ENC (object));
  gst_vpx_enc = GST_VPX_ENC (object);

  g_mutex_lock (&gst_vpx_enc->encoder_lock);
  switch (prop_id) {
    case PROP_RC_END_USAGE:
      gst_vpx_enc->cfg.rc_end_usage = g_value_get_enum (value);
      global = TRUE;
      break;
    case PROP_RC_TARGET_BITRATE:
      if (g_value_get_int (value) == 0) {
        gst_vpx_enc_set_auto_bitrate (gst_vpx_enc);
        gst_vpx_enc->rc_target_bitrate_auto = TRUE;
      } else {
        gst_vpx_enc->cfg.rc_target_bitrate = g_value_get_int (value) / 1000;
        gst_vpx_enc->rc_target_bitrate_auto = FALSE;
      }
      global = TRUE;
      break;
    case PROP_RC_MIN_QUANTIZER:
      gst_vpx_enc->cfg.rc_min_quantizer = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_MAX_QUANTIZER:
      gst_vpx_enc->cfg.rc_max_quantizer = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_DROPFRAME_THRESH:
      gst_vpx_enc->cfg.rc_dropframe_thresh = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_RESIZE_ALLOWED:
      gst_vpx_enc->cfg.rc_resize_allowed = g_value_get_boolean (value);
      global = TRUE;
      break;
    case PROP_RC_RESIZE_UP_THRESH:
      gst_vpx_enc->cfg.rc_resize_up_thresh = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_RESIZE_DOWN_THRESH:
      gst_vpx_enc->cfg.rc_resize_down_thresh = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_UNDERSHOOT_PCT:
      gst_vpx_enc->cfg.rc_undershoot_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_OVERSHOOT_PCT:
      gst_vpx_enc->cfg.rc_overshoot_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_BUF_SZ:
      gst_vpx_enc->cfg.rc_buf_sz = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_BUF_INITIAL_SZ:
      gst_vpx_enc->cfg.rc_buf_initial_sz = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_BUF_OPTIMAL_SZ:
      gst_vpx_enc->cfg.rc_buf_optimal_sz = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_2PASS_VBR_BIAS_PCT:
      gst_vpx_enc->cfg.rc_2pass_vbr_bias_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_2PASS_VBR_MINSECTION_PCT:
      gst_vpx_enc->cfg.rc_2pass_vbr_minsection_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_2PASS_VBR_MAXSECTION_PCT:
      gst_vpx_enc->cfg.rc_2pass_vbr_maxsection_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_KF_MODE:
      gst_vpx_enc->cfg.kf_mode = g_value_get_enum (value);
      global = TRUE;
      break;
    case PROP_KF_MAX_DIST:
      gst_vpx_enc->cfg.kf_max_dist = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_MULTIPASS_MODE:
      gst_vpx_enc->cfg.g_pass = g_value_get_enum (value);
      global = TRUE;
      break;
    case PROP_MULTIPASS_CACHE_FILE:
      if (gst_vpx_enc->multipass_cache_prefix)
        g_free (gst_vpx_enc->multipass_cache_prefix);
      gst_vpx_enc->multipass_cache_prefix = g_value_dup_string (value);
      break;
    case PROP_TS_NUMBER_LAYERS:
      gst_vpx_enc->cfg.ts_number_layers = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_TS_TARGET_BITRATE:{
      GValueArray *va = g_value_get_boxed (value);

      memset (&gst_vpx_enc->cfg.ts_target_bitrate, 0,
          sizeof (gst_vpx_enc->cfg.ts_target_bitrate));
      if (va == NULL) {
        gst_vpx_enc->n_ts_target_bitrate = 0;
      } else if (va->n_values > VPX_TS_MAX_LAYERS) {
        g_warning ("%s: Only %d layers allowed at maximum",
            GST_ELEMENT_NAME (gst_vpx_enc), VPX_TS_MAX_LAYERS);
      } else {
        gint i;

        for (i = 0; i < va->n_values; i++)
          gst_vpx_enc->cfg.ts_target_bitrate[i] =
              g_value_get_int (g_value_array_get_nth (va, i)) / 1000;
        gst_vpx_enc->n_ts_target_bitrate = va->n_values;
      }
      global = TRUE;
      break;
    }
    case PROP_TS_RATE_DECIMATOR:{
      GValueArray *va = g_value_get_boxed (value);

      memset (&gst_vpx_enc->cfg.ts_rate_decimator, 0,
          sizeof (gst_vpx_enc->cfg.ts_rate_decimator));
      if (va == NULL) {
        gst_vpx_enc->n_ts_rate_decimator = 0;
      } else if (va->n_values > VPX_TS_MAX_LAYERS) {
        g_warning ("%s: Only %d layers allowed at maximum",
            GST_ELEMENT_NAME (gst_vpx_enc), VPX_TS_MAX_LAYERS);
      } else {
        gint i;

        for (i = 0; i < va->n_values; i++)
          gst_vpx_enc->cfg.ts_rate_decimator[i] =
              g_value_get_int (g_value_array_get_nth (va, i));
        gst_vpx_enc->n_ts_rate_decimator = va->n_values;
      }
      global = TRUE;
      break;
    }
    case PROP_TS_PERIODICITY:
      gst_vpx_enc->cfg.ts_periodicity = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_TS_LAYER_ID:{
      GValueArray *va = g_value_get_boxed (value);

      memset (&gst_vpx_enc->cfg.ts_layer_id, 0,
          sizeof (gst_vpx_enc->cfg.ts_layer_id));
      if (va && va->n_values > VPX_TS_MAX_PERIODICITY) {
        g_warning ("%s: Only %d sized layer sequences allowed at maximum",
            GST_ELEMENT_NAME (gst_vpx_enc), VPX_TS_MAX_PERIODICITY);
      } else if (va) {
        gint i;

        for (i = 0; i < va->n_values; i++)
          gst_vpx_enc->cfg.ts_layer_id[i] =
              g_value_get_int (g_value_array_get_nth (va, i));
        gst_vpx_enc->n_ts_layer_id = va->n_values;
      } else {
        gst_vpx_enc->n_ts_layer_id = 0;
      }
      global = TRUE;
      break;
    }
    case PROP_TS_LAYER_FLAGS:{
      gint l = gst_value_array_get_size (value);

      g_free (gst_vpx_enc->ts_layer_flags);
      gst_vpx_enc->n_ts_layer_flags = 0;

      if (l > 0) {
        gint i;

        gst_vpx_enc->ts_layer_flags = g_new (gint, l);

        for (i = 0; i < l; i++)
          gst_vpx_enc->ts_layer_flags[i] =
              g_value_get_flags (gst_value_array_get_value (value, i));
        gst_vpx_enc->n_ts_layer_flags = l;
      } else {
        gst_vpx_enc->ts_layer_flags = NULL;
      }
      break;
    }
    case PROP_TS_LAYER_SYNC_FLAGS:{
      gint l = gst_value_array_get_size (value);

      g_free (gst_vpx_enc->ts_layer_sync_flags);
      gst_vpx_enc->n_ts_layer_sync_flags = 0;

      if (l > 0) {
        gint i;

        gst_vpx_enc->ts_layer_sync_flags = g_new (gboolean, l);
        for (i = 0; i < l; i++)
          gst_vpx_enc->ts_layer_sync_flags[i] =
              g_value_get_boolean (gst_value_array_get_value (value, i));
        gst_vpx_enc->n_ts_layer_sync_flags = l;
      } else {
        gst_vpx_enc->ts_layer_sync_flags = NULL;
      }
      break;
    }
    case PROP_ERROR_RESILIENT:
      gst_vpx_enc->cfg.g_error_resilient = g_value_get_flags (value);
      global = TRUE;
      break;
    case PROP_LAG_IN_FRAMES:
      gst_vpx_enc->cfg.g_lag_in_frames = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_THREADS:
      gst_vpx_enc->cfg.g_threads = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_DEADLINE:
      gst_vpx_enc->deadline = g_value_get_int64 (value);
      break;
    case PROP_H_SCALING_MODE:
      gst_vpx_enc->h_scaling_mode = g_value_get_enum (value);
      if (gst_vpx_enc->inited) {
        vpx_scaling_mode_t sm;

        sm.h_scaling_mode = gst_vpx_enc->h_scaling_mode;
        sm.v_scaling_mode = gst_vpx_enc->v_scaling_mode;

        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_SCALEMODE, &sm);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_SCALEMODE",
              status);
        }
      }
      break;
    case PROP_V_SCALING_MODE:
      gst_vpx_enc->v_scaling_mode = g_value_get_enum (value);
      if (gst_vpx_enc->inited) {
        vpx_scaling_mode_t sm;

        sm.h_scaling_mode = gst_vpx_enc->h_scaling_mode;
        sm.v_scaling_mode = gst_vpx_enc->v_scaling_mode;

        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_SCALEMODE, &sm);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_SCALEMODE",
              status);
        }
      }
      break;
    case PROP_CPU_USED:
      gst_vpx_enc->cpu_used = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_CPUUSED,
            gst_vpx_enc->cpu_used);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_CPUUSED",
              status);
        }
      }
      break;
    case PROP_ENABLE_AUTO_ALT_REF:
      gst_vpx_enc->enable_auto_alt_ref = g_value_get_boolean (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_ENABLEAUTOALTREF,
            (gst_vpx_enc->enable_auto_alt_ref ? 1 : 0));
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc,
              "Failed to set VP8E_SET_ENABLEAUTOALTREF", status);
        }
      }
      break;
    case PROP_NOISE_SENSITIVITY:
      gst_vpx_enc->noise_sensitivity = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder,
            VP8E_SET_NOISE_SENSITIVITY, gst_vpx_enc->noise_sensitivity);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc,
              "Failed to set VP8E_SET_NOISE_SENSITIVITY", status);
        }
      }
      break;
    case PROP_SHARPNESS:
      gst_vpx_enc->sharpness = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status = vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_SHARPNESS,
            gst_vpx_enc->sharpness);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_SHARPNESS",
              status);
        }
      }
      break;
    case PROP_STATIC_THRESHOLD:
      gst_vpx_enc->static_threshold = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_STATIC_THRESHOLD,
            gst_vpx_enc->static_threshold);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc,
              "Failed to set VP8E_SET_STATIC_THRESHOLD", status);
        }
      }
      break;
    case PROP_TOKEN_PARTITIONS:
      gst_vpx_enc->token_partitions = g_value_get_enum (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_TOKEN_PARTITIONS,
            gst_vpx_enc->token_partitions);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc,
              "Failed to set VP8E_SET_TOKEN_PARTIONS", status);
        }
      }
      break;
    case PROP_ARNR_MAXFRAMES:
      gst_vpx_enc->arnr_maxframes = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_ARNR_MAXFRAMES,
            gst_vpx_enc->arnr_maxframes);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc,
              "Failed to set VP8E_SET_ARNR_MAXFRAMES", status);
        }
      }
      break;
    case PROP_ARNR_STRENGTH:
      gst_vpx_enc->arnr_strength = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_ARNR_STRENGTH,
            gst_vpx_enc->arnr_strength);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_ARNR_STRENGTH",
              status);
        }
      }
      break;
    case PROP_ARNR_TYPE:
      gst_vpx_enc->arnr_type = g_value_get_int (value);
      g_warning ("arnr-type is a no-op since control has been deprecated "
          "in libvpx");
      break;
    case PROP_TUNING:
      gst_vpx_enc->tuning = g_value_get_enum (value);
      if (gst_vpx_enc->inited) {
        status = vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_TUNING,
            gst_vpx_enc->tuning);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_TUNING",
              status);
        }
      }
      break;
    case PROP_CQ_LEVEL:
      gst_vpx_enc->cq_level = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status = vpx_codec_control (&gst_vpx_enc->encoder, VP8E_SET_CQ_LEVEL,
            gst_vpx_enc->cq_level);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc, "Failed to set VP8E_SET_CQ_LEVEL",
              status);
        }
      }
      break;
    case PROP_MAX_INTRA_BITRATE_PCT:
      gst_vpx_enc->max_intra_bitrate_pct = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder,
            VP8E_SET_MAX_INTRA_BITRATE_PCT, gst_vpx_enc->max_intra_bitrate_pct);
        if (status != VPX_CODEC_OK) {
          GST_VPX_ENC_WARN (gst_vpx_enc,
              "Failed to set VP8E_SET_MAX_INTRA_BITRATE_PCT", status);
        }
      }
      break;
    case PROP_TIMEBASE:
      gst_vpx_enc->timebase_n = gst_value_get_fraction_numerator (value);
      gst_vpx_enc->timebase_d = gst_value_get_fraction_denominator (value);
      break;
    case PROP_BITS_PER_PIXEL:
      gst_vpx_enc->bits_per_pixel = g_value_get_float (value);
      if (gst_vpx_enc->rc_target_bitrate_auto) {
        gst_vpx_enc_set_auto_bitrate (gst_vpx_enc);
        global = TRUE;
      }
      break;
    default:
      break;
  }

  if (global &&gst_vpx_enc->inited) {
    status =
        vpx_codec_enc_config_set (&gst_vpx_enc->encoder, &gst_vpx_enc->cfg);
    if (status != VPX_CODEC_OK) {
      g_mutex_unlock (&gst_vpx_enc->encoder_lock);
      GST_ELEMENT_ERROR_WITH_DETAILS (gst_vpx_enc, LIBRARY, INIT,
          ("Failed to set encoder configuration"), ("%s : %s",
              gst_vpx_error_name (status),
              GST_STR_NULL (gst_vpx_enc->encoder.err_detail)), ("details",
              G_TYPE_STRING, GST_STR_NULL (gst_vpx_enc->encoder.err_detail),
              NULL));
    } else {
      g_mutex_unlock (&gst_vpx_enc->encoder_lock);
    }
  } else {
    g_mutex_unlock (&gst_vpx_enc->encoder_lock);
  }
}

static void
gst_vpx_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVPXEnc *gst_vpx_enc;

  g_return_if_fail (GST_IS_VPX_ENC (object));
  gst_vpx_enc = GST_VPX_ENC (object);

  g_mutex_lock (&gst_vpx_enc->encoder_lock);
  switch (prop_id) {
    case PROP_RC_END_USAGE:
      g_value_set_enum (value, gst_vpx_enc->cfg.rc_end_usage);
      break;
    case PROP_RC_TARGET_BITRATE:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_target_bitrate * 1000);
      break;
    case PROP_RC_MIN_QUANTIZER:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_min_quantizer);
      break;
    case PROP_RC_MAX_QUANTIZER:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_max_quantizer);
      break;
    case PROP_RC_DROPFRAME_THRESH:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_dropframe_thresh);
      break;
    case PROP_RC_RESIZE_ALLOWED:
      g_value_set_boolean (value, gst_vpx_enc->cfg.rc_resize_allowed);
      break;
    case PROP_RC_RESIZE_UP_THRESH:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_resize_up_thresh);
      break;
    case PROP_RC_RESIZE_DOWN_THRESH:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_resize_down_thresh);
      break;
    case PROP_RC_UNDERSHOOT_PCT:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_undershoot_pct);
      break;
    case PROP_RC_OVERSHOOT_PCT:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_overshoot_pct);
      break;
    case PROP_RC_BUF_SZ:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_buf_sz);
      break;
    case PROP_RC_BUF_INITIAL_SZ:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_buf_initial_sz);
      break;
    case PROP_RC_BUF_OPTIMAL_SZ:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_buf_optimal_sz);
      break;
    case PROP_RC_2PASS_VBR_BIAS_PCT:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_2pass_vbr_bias_pct);
      break;
    case PROP_RC_2PASS_VBR_MINSECTION_PCT:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_2pass_vbr_minsection_pct);
      break;
    case PROP_RC_2PASS_VBR_MAXSECTION_PCT:
      g_value_set_int (value, gst_vpx_enc->cfg.rc_2pass_vbr_maxsection_pct);
      break;
    case PROP_KF_MODE:
      g_value_set_enum (value, gst_vpx_enc->cfg.kf_mode);
      break;
    case PROP_KF_MAX_DIST:
      g_value_set_int (value, gst_vpx_enc->cfg.kf_max_dist);
      break;
    case PROP_MULTIPASS_MODE:
      g_value_set_enum (value, gst_vpx_enc->cfg.g_pass);
      break;
    case PROP_MULTIPASS_CACHE_FILE:
      g_value_set_string (value, gst_vpx_enc->multipass_cache_prefix);
      break;
    case PROP_TS_NUMBER_LAYERS:
      g_value_set_int (value, gst_vpx_enc->cfg.ts_number_layers);
      break;
    case PROP_TS_TARGET_BITRATE:{
      GValueArray *va;

      if (gst_vpx_enc->n_ts_target_bitrate == 0) {
        g_value_set_boxed (value, NULL);
      } else {
        gint i;

        va = g_value_array_new (gst_vpx_enc->n_ts_target_bitrate);
        for (i = 0; i < gst_vpx_enc->n_ts_target_bitrate; i++) {
          GValue v = { 0, };

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, gst_vpx_enc->cfg.ts_target_bitrate[i] * 1000);
          g_value_array_append (va, &v);
          g_value_unset (&v);
        }
        g_value_set_boxed (value, va);
        g_value_array_free (va);
      }
      break;
    }
    case PROP_TS_RATE_DECIMATOR:{
      GValueArray *va;

      if (gst_vpx_enc->n_ts_rate_decimator == 0) {
        g_value_set_boxed (value, NULL);
      } else {
        gint i;

        va = g_value_array_new (gst_vpx_enc->n_ts_rate_decimator);
        for (i = 0; i < gst_vpx_enc->n_ts_rate_decimator; i++) {
          GValue v = { 0, };

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, gst_vpx_enc->cfg.ts_rate_decimator[i]);
          g_value_array_append (va, &v);
          g_value_unset (&v);
        }
        g_value_set_boxed (value, va);
        g_value_array_free (va);
      }
      break;
    }
    case PROP_TS_PERIODICITY:
      g_value_set_int (value, gst_vpx_enc->cfg.ts_periodicity);
      break;
    case PROP_TS_LAYER_ID:{
      GValueArray *va;

      if (gst_vpx_enc->n_ts_layer_id == 0) {
        g_value_set_boxed (value, NULL);
      } else {
        gint i;

        va = g_value_array_new (gst_vpx_enc->n_ts_layer_id);
        for (i = 0; i < gst_vpx_enc->n_ts_layer_id; i++) {
          GValue v = { 0, };

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, gst_vpx_enc->cfg.ts_layer_id[i]);
          g_value_array_append (va, &v);
          g_value_unset (&v);
        }
        g_value_set_boxed (value, va);
        g_value_array_free (va);
      }
      break;
    }
    case PROP_TS_LAYER_FLAGS:{
      gint i;

      for (i = 0; i < gst_vpx_enc->n_ts_layer_flags; i++) {
        GValue v = { 0, };

        g_value_init (&v, GST_VPX_ENC_TS_LAYER_FLAGS_TYPE);
        g_value_set_flags (&v, gst_vpx_enc->ts_layer_flags[i]);
        gst_value_array_append_value (value, &v);
        g_value_unset (&v);
      }
      break;
    }
    case PROP_TS_LAYER_SYNC_FLAGS:{
      gint i;

      for (i = 0; i < gst_vpx_enc->n_ts_layer_sync_flags; i++) {
        GValue v = { 0, };

        g_value_init (&v, G_TYPE_BOOLEAN);
        g_value_set_boolean (&v, gst_vpx_enc->ts_layer_sync_flags[i]);
        gst_value_array_append_value (value, &v);
        g_value_unset (&v);
      }
      break;
    }
    case PROP_ERROR_RESILIENT:
      g_value_set_flags (value, gst_vpx_enc->cfg.g_error_resilient);
      break;
    case PROP_LAG_IN_FRAMES:
      g_value_set_int (value, gst_vpx_enc->cfg.g_lag_in_frames);
      break;
    case PROP_THREADS:
      g_value_set_int (value, gst_vpx_enc->cfg.g_threads);
      break;
    case PROP_DEADLINE:
      g_value_set_int64 (value, gst_vpx_enc->deadline);
      break;
    case PROP_H_SCALING_MODE:
      g_value_set_enum (value, gst_vpx_enc->h_scaling_mode);
      break;
    case PROP_V_SCALING_MODE:
      g_value_set_enum (value, gst_vpx_enc->v_scaling_mode);
      break;
    case PROP_CPU_USED:
      g_value_set_int (value, gst_vpx_enc->cpu_used);
      break;
    case PROP_ENABLE_AUTO_ALT_REF:
      g_value_set_boolean (value, gst_vpx_enc->enable_auto_alt_ref);
      break;
    case PROP_NOISE_SENSITIVITY:
      g_value_set_int (value, gst_vpx_enc->noise_sensitivity);
      break;
    case PROP_SHARPNESS:
      g_value_set_int (value, gst_vpx_enc->sharpness);
      break;
    case PROP_STATIC_THRESHOLD:
      g_value_set_int (value, gst_vpx_enc->static_threshold);
      break;
    case PROP_TOKEN_PARTITIONS:
      g_value_set_enum (value, gst_vpx_enc->token_partitions);
      break;
    case PROP_ARNR_MAXFRAMES:
      g_value_set_int (value, gst_vpx_enc->arnr_maxframes);
      break;
    case PROP_ARNR_STRENGTH:
      g_value_set_int (value, gst_vpx_enc->arnr_strength);
      break;
    case PROP_ARNR_TYPE:
      g_value_set_int (value, gst_vpx_enc->arnr_type);
      break;
    case PROP_TUNING:
      g_value_set_enum (value, gst_vpx_enc->tuning);
      break;
    case PROP_CQ_LEVEL:
      g_value_set_int (value, gst_vpx_enc->cq_level);
      break;
    case PROP_MAX_INTRA_BITRATE_PCT:
      g_value_set_int (value, gst_vpx_enc->max_intra_bitrate_pct);
      break;
    case PROP_TIMEBASE:
      gst_value_set_fraction (value, gst_vpx_enc->timebase_n,
          gst_vpx_enc->timebase_d);
      break;
    case PROP_BITS_PER_PIXEL:
      g_value_set_float (value, gst_vpx_enc->bits_per_pixel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&gst_vpx_enc->encoder_lock);
}

static gboolean
gst_vpx_enc_start (GstVideoEncoder * video_encoder)
{
  GstVPXEnc *encoder = GST_VPX_ENC (video_encoder);

  GST_DEBUG_OBJECT (video_encoder, "start");

  if (!encoder->have_default_config) {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT,
        ("Failed to get default encoder configuration"), (NULL));
    return FALSE;
  }

  return TRUE;
}

static void
gst_vpx_enc_destroy_encoder (GstVPXEnc * encoder)
{
  g_mutex_lock (&encoder->encoder_lock);
  if (encoder->inited) {
    vpx_codec_destroy (&encoder->encoder);
    encoder->inited = FALSE;
  }

  if (encoder->first_pass_cache_content) {
    g_byte_array_free (encoder->first_pass_cache_content, TRUE);
    encoder->first_pass_cache_content = NULL;
  }

  if (encoder->cfg.rc_twopass_stats_in.buf) {
    g_free (encoder->cfg.rc_twopass_stats_in.buf);
    encoder->cfg.rc_twopass_stats_in.buf = NULL;
    encoder->cfg.rc_twopass_stats_in.sz = 0;
  }

  encoder->last_pts = GST_CLOCK_TIME_NONE;
  encoder->last_input_duration = GST_CLOCK_TIME_NONE;

  g_mutex_unlock (&encoder->encoder_lock);
}

static gboolean
gst_vpx_enc_stop (GstVideoEncoder * video_encoder)
{
  GstVPXEnc *encoder;

  GST_DEBUG_OBJECT (video_encoder, "stop");

  encoder = GST_VPX_ENC (video_encoder);

  gst_vpx_enc_destroy_encoder (encoder);

  gst_tag_setter_reset_tags (GST_TAG_SETTER (encoder));

  g_free (encoder->multipass_cache_file);
  encoder->multipass_cache_file = NULL;
  encoder->multipass_cache_idx = 0;

  return TRUE;
}

#define INVALID_PROFILE -1

static gint
gst_vpx_gvalue_to_profile (const GValue * v)
{
  gchar *endptr = NULL;
  gint profile = g_ascii_strtoull (g_value_get_string (v), &endptr, 10);

  if (*endptr != '\0') {
    profile = INVALID_PROFILE;
  }

  return profile;
}

static gint
gst_vpx_enc_get_downstream_profile (GstVPXEnc * encoder, GstVideoInfo * info)
{
  GstCaps *allowed;
  GstStructure *s;
  gint min_profile;
  gint profile = INVALID_PROFILE;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_Y444:
      min_profile = 1;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
      min_profile = 2;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
      min_profile = 3;
      break;
    default:
      min_profile = 0;
  }

  allowed = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (allowed) {
    allowed = gst_caps_truncate (allowed);
    s = gst_caps_get_structure (allowed, 0);
    if (gst_structure_has_field (s, "profile")) {
      const GValue *v = gst_structure_get_value (s, "profile");

      if (GST_VALUE_HOLDS_LIST (v)) {
        gint i;

        for (i = 0; i != gst_value_list_get_size (v); ++i) {
          gint p = gst_vpx_gvalue_to_profile (gst_value_list_get_value (v, i));
          if (p >= min_profile) {
            profile = p;
            break;
          }
        }
      } else if (G_VALUE_HOLDS_STRING (v)) {
        profile = gst_vpx_gvalue_to_profile (v);
      }

      if (profile < min_profile || profile > 3) {
        profile = INVALID_PROFILE;
      }

      if (profile > 1 && info->finfo->bits == 8) {
        GST_DEBUG_OBJECT (encoder,
            "Codec bit-depth 8 not supported in profile > 1");
        profile = INVALID_PROFILE;
      }
    }
    gst_caps_unref (allowed);
  }

  GST_DEBUG_OBJECT (encoder, "Using profile %d", profile);

  return profile;
}

static gboolean
gst_vpx_enc_set_format (GstVideoEncoder * video_encoder,
    GstVideoCodecState * state)
{
  GstVPXEnc *encoder;
  vpx_codec_err_t status;
  vpx_image_t *image;
  vpx_codec_flags_t flags = 0;
  GstCaps *caps;
  gboolean ret = TRUE;
  GstVideoInfo *info = &state->info;
  GstVideoCodecState *output_state;
  GstClockTime latency;
  GstVPXEncClass *vpx_enc_class;

  encoder = GST_VPX_ENC (video_encoder);
  vpx_enc_class = GST_VPX_ENC_GET_CLASS (encoder);
  GST_DEBUG_OBJECT (video_encoder, "set_format");

  if (encoder->inited) {
    gst_vpx_enc_drain (video_encoder);
    g_mutex_lock (&encoder->encoder_lock);
    vpx_codec_destroy (&encoder->encoder);
    encoder->inited = FALSE;
    encoder->multipass_cache_idx++;
  } else {
    g_mutex_lock (&encoder->encoder_lock);
    encoder->last_pts = GST_CLOCK_TIME_NONE;
    encoder->last_input_duration = GST_CLOCK_TIME_NONE;
  }

  encoder->cfg.g_bit_depth = encoder->cfg.g_input_bit_depth = info->finfo->bits;
  if (encoder->cfg.g_bit_depth > 8) {
    flags |= VPX_CODEC_USE_HIGHBITDEPTH;
  }

  encoder->cfg.g_profile = gst_vpx_enc_get_downstream_profile (encoder, info);
  if (encoder->cfg.g_profile == INVALID_PROFILE) {
    GST_ELEMENT_ERROR (encoder, RESOURCE, OPEN_READ,
        ("Invalid vpx profile"), (NULL));
    g_mutex_unlock (&encoder->encoder_lock);
    return FALSE;
  }

  encoder->cfg.g_w = GST_VIDEO_INFO_WIDTH (info);
  encoder->cfg.g_h = GST_VIDEO_INFO_HEIGHT (info);

  if (encoder->timebase_n != 0 && encoder->timebase_d != 0) {
    GST_DEBUG_OBJECT (video_encoder, "Using timebase configuration");
    encoder->cfg.g_timebase.num = encoder->timebase_n;
    encoder->cfg.g_timebase.den = encoder->timebase_d;
  } else {
    /* Zero framerate and max-framerate but still need to setup the timebase to avoid
     * a divide by zero error. Presuming the lowest common denominator will be RTP -
     * VP8 payload draft states clock rate of 90000 which should work for anyone where
     * FPS < 90000 (shouldn't be too many cases where it's higher) though wouldn't be optimal. RTP specification
     * http://tools.ietf.org/html/draft-ietf-payload-vp8-01 section 6.3.1 */
    encoder->cfg.g_timebase.num = 1;
    encoder->cfg.g_timebase.den = 90000;
  }

  if (encoder->cfg.g_pass == VPX_RC_FIRST_PASS ||
      encoder->cfg.g_pass == VPX_RC_LAST_PASS) {
    if (!encoder->multipass_cache_prefix) {
      GST_ELEMENT_ERROR (encoder, RESOURCE, OPEN_READ,
          ("No multipass cache file provided"), (NULL));
      g_mutex_unlock (&encoder->encoder_lock);
      return FALSE;
    }

    g_free (encoder->multipass_cache_file);

    if (encoder->multipass_cache_idx > 0)
      encoder->multipass_cache_file = g_strdup_printf ("%s.%u",
          encoder->multipass_cache_prefix, encoder->multipass_cache_idx);
    else
      encoder->multipass_cache_file =
          g_strdup (encoder->multipass_cache_prefix);
  }

  if (encoder->cfg.g_pass == VPX_RC_FIRST_PASS) {
    if (encoder->first_pass_cache_content != NULL)
      g_byte_array_free (encoder->first_pass_cache_content, TRUE);

    encoder->first_pass_cache_content = g_byte_array_sized_new (4096);

  } else if (encoder->cfg.g_pass == VPX_RC_LAST_PASS) {
    GError *err = NULL;

    if (encoder->cfg.rc_twopass_stats_in.buf != NULL) {
      g_free (encoder->cfg.rc_twopass_stats_in.buf);
      encoder->cfg.rc_twopass_stats_in.buf = NULL;
      encoder->cfg.rc_twopass_stats_in.sz = 0;
    }

    if (!g_file_get_contents (encoder->multipass_cache_file,
            (gchar **) & encoder->cfg.rc_twopass_stats_in.buf,
            &encoder->cfg.rc_twopass_stats_in.sz, &err)) {
      GST_ELEMENT_ERROR (encoder, RESOURCE, OPEN_READ,
          ("Failed to read multipass cache file provided"), ("%s",
              err->message));
      g_error_free (err);
      g_mutex_unlock (&encoder->encoder_lock);
      return FALSE;
    }
  }

  status =
      vpx_codec_enc_init (&encoder->encoder, vpx_enc_class->get_algo (encoder),
      &encoder->cfg, flags);
  if (status != VPX_CODEC_OK) {
    GST_ELEMENT_ERROR_WITH_DETAILS (encoder, LIBRARY, INIT,
        ("Failed to initialize encoder"), ("%s : %s",
            gst_vpx_error_name (status),
            GST_STR_NULL (encoder->encoder.err_detail)), ("details",
            G_TYPE_STRING, GST_STR_NULL (encoder->encoder.err_detail), NULL));
    g_mutex_unlock (&encoder->encoder_lock);
    return FALSE;
  }

  if (vpx_enc_class->enable_scaling (encoder)) {
    vpx_scaling_mode_t sm;

    sm.h_scaling_mode = encoder->h_scaling_mode;
    sm.v_scaling_mode = encoder->v_scaling_mode;

    status = vpx_codec_control (&encoder->encoder, VP8E_SET_SCALEMODE, &sm);
    if (status != VPX_CODEC_OK) {
      GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_SCALEMODE", status);
    }
  }

  status =
      vpx_codec_control (&encoder->encoder, VP8E_SET_CPUUSED,
      encoder->cpu_used);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_CPUUSED", status);
  }

  status =
      vpx_codec_control (&encoder->encoder, VP8E_SET_ENABLEAUTOALTREF,
      (encoder->enable_auto_alt_ref ? 1 : 0));
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_ENABLEAUTOALTREF",
        status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_NOISE_SENSITIVITY,
      encoder->noise_sensitivity);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_NOISE_SENSITIVITY",
        status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_SHARPNESS,
      encoder->sharpness);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_SHARPNESS", status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_STATIC_THRESHOLD,
      encoder->static_threshold);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_STATIC_THRESHOLD",
        status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_TOKEN_PARTITIONS,
      encoder->token_partitions);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_TOKEN_PARTIONS", status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_ARNR_MAXFRAMES,
      encoder->arnr_maxframes);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_ARNR_MAXFRAMES", status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_ARNR_STRENGTH,
      encoder->arnr_strength);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_ARNR_STRENGTH", status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_TUNING,
      encoder->tuning);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_TUNING", status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_CQ_LEVEL,
      encoder->cq_level);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_CQ_LEVEL", status);
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_MAX_INTRA_BITRATE_PCT,
      encoder->max_intra_bitrate_pct);
  if (status != VPX_CODEC_OK) {
    GST_VPX_ENC_WARN (encoder, "Failed to set VP8E_SET_MAX_INTRA_BITRATE_PCT",
        status);
  }

  if (vpx_enc_class->configure_encoder
      && !vpx_enc_class->configure_encoder (encoder, state)) {
    ret = FALSE;
    g_mutex_unlock (&encoder->encoder_lock);
    goto done;
  }

  if (GST_VIDEO_INFO_FPS_D (info) == 0 || GST_VIDEO_INFO_FPS_N (info) == 0) {
    /* FIXME: Assume 25fps for unknown framerates. Better than reporting
     * that we introduce no latency while we actually do
     */
    latency = gst_util_uint64_scale (encoder->cfg.g_lag_in_frames,
        1 * GST_SECOND, 25);
  } else {
    latency = gst_util_uint64_scale (encoder->cfg.g_lag_in_frames,
        GST_VIDEO_INFO_FPS_D (info) * GST_SECOND, GST_VIDEO_INFO_FPS_N (info));
  }
  gst_video_encoder_set_latency (video_encoder, latency, latency);
  encoder->inited = TRUE;

  /* Store input state */
  if (encoder->input_state)
    gst_video_codec_state_unref (encoder->input_state);
  encoder->input_state = gst_video_codec_state_ref (state);

  /* Scale default bitrate to our size */
  if (encoder->rc_target_bitrate_auto)
    gst_vpx_enc_set_auto_bitrate (encoder);

  /* prepare cached image buffer setup */
  image = &encoder->image;
  memset (image, 0, sizeof (*image));

  vpx_enc_class->set_image_format (encoder, image);

  image->w = image->d_w = GST_VIDEO_INFO_WIDTH (info);
  image->h = image->d_h = GST_VIDEO_INFO_HEIGHT (info);

  image->stride[VPX_PLANE_Y] = GST_VIDEO_INFO_COMP_STRIDE (info, 0);
  image->stride[VPX_PLANE_U] = GST_VIDEO_INFO_COMP_STRIDE (info, 1);
  image->stride[VPX_PLANE_V] = GST_VIDEO_INFO_COMP_STRIDE (info, 2);

  caps = vpx_enc_class->get_new_vpx_caps (encoder);

  vpx_enc_class->set_stream_info (encoder, caps, info);

  g_mutex_unlock (&encoder->encoder_lock);

  output_state =
      gst_video_encoder_set_output_state (video_encoder, caps, state);
  gst_video_codec_state_unref (output_state);

  gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));

done:
  return ret;
}

static GstFlowReturn
gst_vpx_enc_process (GstVPXEnc * encoder)
{
  vpx_codec_iter_t iter = NULL;
  const vpx_codec_cx_pkt_t *pkt;
  GstVideoEncoder *video_encoder;
  void *user_data;
  GstVideoCodecFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVPXEncClass *vpx_enc_class;
  vpx_codec_pts_t pts;
  guint layer_id = 0;
  guint8 tl0picidx = 0;
  gboolean layer_sync = FALSE;

  video_encoder = GST_VIDEO_ENCODER (encoder);
  vpx_enc_class = GST_VPX_ENC_GET_CLASS (encoder);

  g_mutex_lock (&encoder->encoder_lock);
  pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  while (pkt != NULL) {
    GstBuffer *buffer;
    gboolean invisible;

    GST_DEBUG_OBJECT (encoder, "packet %u type %d", (guint) pkt->data.frame.sz,
        pkt->kind);

    if (pkt->kind == VPX_CODEC_STATS_PKT
        && encoder->cfg.g_pass == VPX_RC_FIRST_PASS) {
      GST_LOG_OBJECT (encoder, "handling STATS packet");

      g_byte_array_append (encoder->first_pass_cache_content,
          pkt->data.twopass_stats.buf, pkt->data.twopass_stats.sz);

      frame = gst_video_encoder_get_oldest_frame (video_encoder);
      if (frame != NULL) {
        buffer = gst_buffer_new ();
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_LIVE);
        frame->output_buffer = buffer;
        g_mutex_unlock (&encoder->encoder_lock);
        ret = gst_video_encoder_finish_frame (video_encoder, frame);
        g_mutex_lock (&encoder->encoder_lock);
      }

      pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
      continue;
    } else if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
      GST_LOG_OBJECT (encoder, "non frame pkt: %d", pkt->kind);
      pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
      continue;
    }

    invisible = (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE) != 0;

    /* discard older frames that were dropped by libvpx */
    frame = NULL;
    do {
      GstClockTime pts_rt;

      if (frame)
        gst_video_encoder_finish_frame (video_encoder, frame);
      frame = gst_video_encoder_get_oldest_frame (video_encoder);
      if (!frame) {
        GST_WARNING_OBJECT (encoder,
            "vpx pts %" G_GINT64_FORMAT
            " does not match input frames, discarding", pkt->data.frame.pts);
        goto out;
      }

      pts_rt =
          gst_segment_to_running_time (&video_encoder->input_segment,
          GST_FORMAT_TIME, frame->pts);

      pts =
          gst_util_uint64_scale (pts_rt,
          encoder->cfg.g_timebase.den,
          encoder->cfg.g_timebase.num * (GstClockTime) GST_SECOND);
      GST_TRACE_OBJECT (encoder, "vpx pts: %" G_GINT64_FORMAT
          ", gst frame pts: %" G_GINT64_FORMAT, (gint64) pkt->data.frame.pts,
          (gint64) pts);
    } while (pkt->data.frame.pts > pts);

    g_assert (frame != NULL);

    /* FIXME : It would be nice to avoid the memory copy ... */
    buffer = gst_buffer_new_memdup (pkt->data.frame.buf, pkt->data.frame.sz);

    user_data = vpx_enc_class->process_frame_user_data (encoder, frame);
    if (vpx_enc_class->get_frame_temporal_settings &&
        encoder->cfg.ts_periodicity != 0) {
      vpx_enc_class->get_frame_temporal_settings (encoder, frame,
          &layer_id, &tl0picidx, &layer_sync);
    }

    if (layer_id != 0 && encoder->prev_was_keyframe) {
      /* Non-base layer frame immediately after a keyframe is a layer sync */
      layer_sync = TRUE;
    }

    if ((pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      /* Key frames always live on layer 0 */
      layer_id = 0;
      layer_sync = TRUE;
      encoder->prev_was_keyframe = TRUE;
    } else {
      GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
      encoder->prev_was_keyframe = FALSE;
    }

    if ((pkt->data.frame.flags & VPX_FRAME_IS_DROPPABLE) != 0)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DROPPABLE);
    else
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DROPPABLE);

    if (layer_id == 0) {
      /* Allocate a new tl0picidx if this is layer 0 */
      tl0picidx = ++encoder->tl0picidx;
    }

    if (vpx_enc_class->preflight_buffer) {
      vpx_enc_class->preflight_buffer (encoder, frame, buffer,
          layer_sync, layer_id, tl0picidx);
    }

    if (invisible) {
      ret =
          vpx_enc_class->handle_invisible_frame_buffer (encoder, user_data,
          buffer);
      gst_video_codec_frame_unref (frame);
    } else {
      frame->output_buffer = buffer;
      g_mutex_unlock (&encoder->encoder_lock);
      ret = gst_video_encoder_finish_frame (video_encoder, frame);
      g_mutex_lock (&encoder->encoder_lock);
    }

    pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  }

out:
  g_mutex_unlock (&encoder->encoder_lock);

  return ret;
}

/* This function should be called holding then stream lock*/
static GstFlowReturn
gst_vpx_enc_drain (GstVideoEncoder * video_encoder)
{
  GstVPXEnc *encoder;
  int flags = 0;
  vpx_codec_err_t status;
  gint64 deadline;
  vpx_codec_pts_t pts;
  GstClockTime gst_pts = 0;

  encoder = GST_VPX_ENC (video_encoder);

  g_mutex_lock (&encoder->encoder_lock);
  deadline = encoder->deadline;

  if (GST_CLOCK_TIME_IS_VALID (encoder->last_pts))
    gst_pts = encoder->last_pts;
  if (GST_CLOCK_TIME_IS_VALID (encoder->last_input_duration))
    gst_pts += encoder->last_input_duration;

  pts =
      gst_util_uint64_scale (gst_pts,
      encoder->cfg.g_timebase.den,
      encoder->cfg.g_timebase.num * (GstClockTime) GST_SECOND);

  status = vpx_codec_encode (&encoder->encoder, NULL, pts, 0, flags, deadline);
  g_mutex_unlock (&encoder->encoder_lock);

  if (status != 0) {
    GST_ERROR_OBJECT (encoder, "encode returned %d %s (details: %s)", status,
        gst_vpx_error_name (status),
        GST_STR_NULL (encoder->encoder.err_detail));
    return GST_FLOW_ERROR;
  }

  /* dispatch remaining frames */
  gst_vpx_enc_process (encoder);

  g_mutex_lock (&encoder->encoder_lock);
  if (encoder->cfg.g_pass == VPX_RC_FIRST_PASS && encoder->multipass_cache_file) {
    GError *err = NULL;

    if (!g_file_set_contents (encoder->multipass_cache_file,
            (const gchar *) encoder->first_pass_cache_content->data,
            encoder->first_pass_cache_content->len, &err)) {
      GST_ELEMENT_ERROR (encoder, RESOURCE, WRITE, (NULL),
          ("Failed to write multipass cache file: %s", err->message));
      g_error_free (err);
    }
  }
  g_mutex_unlock (&encoder->encoder_lock);

  return GST_FLOW_OK;
}

static gboolean
gst_vpx_enc_flush (GstVideoEncoder * video_encoder)
{
  GstVPXEnc *encoder;

  GST_DEBUG_OBJECT (video_encoder, "flush");

  encoder = GST_VPX_ENC (video_encoder);

  gst_vpx_enc_destroy_encoder (encoder);
  if (encoder->input_state) {
    gst_video_codec_state_ref (encoder->input_state);
    gst_vpx_enc_set_format (video_encoder, encoder->input_state);
    gst_video_codec_state_unref (encoder->input_state);
  }

  return TRUE;
}

static GstFlowReturn
gst_vpx_enc_finish (GstVideoEncoder * video_encoder)
{
  GstVPXEnc *encoder;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (video_encoder, "finish");

  encoder = GST_VPX_ENC (video_encoder);

  if (encoder->inited) {
    ret = gst_vpx_enc_drain (video_encoder);
  } else {
    ret = GST_FLOW_OK;
  }

  return ret;
}

static vpx_image_t *
gst_vpx_enc_buffer_to_image (GstVPXEnc * enc, GstVideoFrame * frame)
{
  vpx_image_t *image = g_new (vpx_image_t, 1);

  memcpy (image, &enc->image, sizeof (*image));

  image->planes[VPX_PLANE_Y] = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  image->planes[VPX_PLANE_U] = GST_VIDEO_FRAME_COMP_DATA (frame, 1);
  image->planes[VPX_PLANE_V] = GST_VIDEO_FRAME_COMP_DATA (frame, 2);

  image->stride[VPX_PLANE_Y] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  image->stride[VPX_PLANE_U] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 1);
  image->stride[VPX_PLANE_V] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 2);

  return image;
}

static GstFlowReturn
gst_vpx_enc_handle_frame (GstVideoEncoder * video_encoder,
    GstVideoCodecFrame * frame)
{
  GstVPXEnc *encoder;
  vpx_codec_err_t status;
  int flags = 0;
  vpx_image_t *image;
  GstVideoFrame vframe;
  GstClockTime pts_rt;
  vpx_codec_pts_t pts;
  unsigned long duration;
  GstVPXEncClass *vpx_enc_class;

  GST_DEBUG_OBJECT (video_encoder, "handle_frame");

  encoder = GST_VPX_ENC (video_encoder);
  vpx_enc_class = GST_VPX_ENC_GET_CLASS (encoder);

  GST_DEBUG_OBJECT (video_encoder, "size %d %d",
      GST_VIDEO_INFO_WIDTH (&encoder->input_state->info),
      GST_VIDEO_INFO_HEIGHT (&encoder->input_state->info));

  gst_video_frame_map (&vframe, &encoder->input_state->info,
      frame->input_buffer, GST_MAP_READ);
  image = gst_vpx_enc_buffer_to_image (encoder, &vframe);

  vpx_enc_class->set_frame_user_data (encoder, frame, image);

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    flags |= VPX_EFLAG_FORCE_KF;
  }

  g_mutex_lock (&encoder->encoder_lock);

  /* the input pts needs to be strictly increasing, see vpx_codec_encode() doc, so convert it to
   * running time as we don't want to reset the encoder for each segment. */
  pts_rt =
      gst_segment_to_running_time (&video_encoder->input_segment,
      GST_FORMAT_TIME, frame->pts);

  /* vpx_codec_encode() enforces us to pass strictly increasing pts */
  if (GST_CLOCK_TIME_IS_VALID (encoder->last_pts)
      && pts_rt <= encoder->last_pts) {
    GST_WARNING_OBJECT (encoder,
        "decreasing pts %" GST_TIME_FORMAT " previous buffer was %"
        GST_TIME_FORMAT " enforce increasing pts", GST_TIME_ARGS (pts_rt),
        GST_TIME_ARGS (encoder->last_pts));
    pts_rt = encoder->last_pts + 1;
  }

  pts =
      gst_util_uint64_scale (pts_rt,
      encoder->cfg.g_timebase.den,
      encoder->cfg.g_timebase.num * (GstClockTime) GST_SECOND);
  encoder->last_pts = pts_rt;

  if (frame->duration != GST_CLOCK_TIME_NONE) {
    duration =
        gst_util_uint64_scale (frame->duration, encoder->cfg.g_timebase.den,
        encoder->cfg.g_timebase.num * (GstClockTime) GST_SECOND);

    if (duration > 0) {
      encoder->last_input_duration = frame->duration;
    } else {
      /* We force the path ignoring the duration if we end up with a zero
       * value for duration after scaling (e.g. duration value too small) */
      GST_WARNING_OBJECT (encoder,
          "Ignoring too small frame duration %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->duration));
      duration = 1;
    }
  } else {
    duration = 1;
  }

  if (encoder->n_ts_layer_flags != 0) {
    /* If we need a keyframe, then the pattern is irrelevant */
    if ((flags & VPX_EFLAG_FORCE_KF) == 0) {
      flags |=
          encoder->ts_layer_flags[frame->system_frame_number %
          encoder->n_ts_layer_flags];
    }
  }

  if (vpx_enc_class->apply_frame_temporal_settings &&
      encoder->cfg.ts_periodicity != 0 &&
      encoder->n_ts_layer_id >= encoder->cfg.ts_periodicity) {
    vpx_enc_class->apply_frame_temporal_settings (encoder, frame,
        encoder->cfg.ts_layer_id[frame->system_frame_number %
            encoder->cfg.ts_periodicity], encoder->tl0picidx,
        encoder->ts_layer_sync_flags[frame->system_frame_number %
            encoder->n_ts_layer_sync_flags]);
  }

  status = vpx_codec_encode (&encoder->encoder, image,
      pts, duration, flags, encoder->deadline);

  g_mutex_unlock (&encoder->encoder_lock);
  gst_video_frame_unmap (&vframe);

  if (status != 0) {
    GST_ELEMENT_ERROR_WITH_DETAILS (encoder, LIBRARY, ENCODE,
        ("Failed to encode frame"), ("%s : %s", gst_vpx_error_name (status),
            GST_STR_NULL (encoder->encoder.err_detail)), ("details",
            G_TYPE_STRING, GST_STR_NULL (encoder->encoder.err_detail), NULL));
    gst_video_codec_frame_set_user_data (frame, NULL, NULL);
    gst_video_codec_frame_unref (frame);

    return GST_FLOW_ERROR;
  }
  gst_video_codec_frame_unref (frame);
  return gst_vpx_enc_process (encoder);
}

static gboolean
gst_vpx_enc_sink_event (GstVideoEncoder * benc, GstEvent * event)
{
  GstVPXEnc *enc = GST_VPX_ENC (benc);

  /* FIXME : Move this to base encoder class */

  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
    GstTagList *list;
    GstTagSetter *setter = GST_TAG_SETTER (enc);
    const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

    gst_event_parse_tag (event, &list);
    gst_tag_setter_merge_tags (setter, list, mode);
  }

  /* just peeked, baseclass handles the rest */
  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_event (benc, event);
}

static gboolean
gst_vpx_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static gboolean
gst_vpx_enc_transform_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, GstMeta * meta)
{
  const GstMetaInfo *info = meta->info;
  gboolean ret = FALSE;

  /* Do not copy GstVP8Meta from input to output buffer */
  if (gst_meta_info_is_custom (info)
      && gst_custom_meta_has_name ((GstCustomMeta *) meta, "GstVP8Meta"))
    goto done;

  ret = TRUE;

done:
  return ret;
}

#endif /* HAVE_VP8_ENCODER || HAVE_VP9_ENCODER */
