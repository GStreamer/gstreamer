/* VP9
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010-2013 Sebastian Dröge <slomo@circular-chaos.org>
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
/**
 * SECTION:element-vp9enc
 * @see_also: vp9dec, webmmux, oggmux
 *
 * This element encodes raw video into a VP9 stream.
 * <ulink url="http://www.webmproject.org">VP9</ulink> is a royalty-free
 * video codec maintained by <ulink url="http://www.google.com/">Google
 * </ulink>. It's the successor of On2 VP3, which was the base of the
 * Theora video codec.
 *
 * To control the quality of the encoding, the #GstVP9Enc::target-bitrate,
 * #GstVP9Enc::min-quantizer, #GstVP9Enc::max-quantizer or #GstVP9Enc::cq-level
 * properties can be used. Which one is used depends on the mode selected by
 * the #GstVP9Enc::end-usage property.
 * See <ulink url="http://www.webmproject.org/docs/encoder-parameters/">Encoder Parameters</ulink>
 * for explanation, examples for useful encoding parameters and more details
 * on the encoding parameters.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! vp9enc ! webmmux ! filesink location=videotestsrc.webm
 * ]| This example pipeline will encode a test video source to VP9 muxed in an
 * WebM container.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VP9_ENCODER

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
#include "gstvp9enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_vp9enc_debug);
#define GST_CAT_DEFAULT gst_vp9enc_debug

/* From vp9/vp9_cx_iface.c */
#define DEFAULT_PROFILE 0

#define DEFAULT_RC_END_USAGE VPX_VBR
#define DEFAULT_RC_TARGET_BITRATE 256000
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

#define DEFAULT_ERROR_RESILIENT 0
#define DEFAULT_LAG_IN_FRAMES 0

#define DEFAULT_THREADS 0

#define DEFAULT_H_SCALING_MODE VP8E_NORMAL
#define DEFAULT_V_SCALING_MODE VP8E_NORMAL
#define DEFAULT_CPU_USED 0
#define DEFAULT_ENABLE_AUTO_ALT_REF FALSE
#define DEFAULT_DEADLINE VPX_DL_BEST_QUALITY
#define DEFAULT_NOISE_SENSITIVITY 0
#define DEFAULT_SHARPNESS 0
#define DEFAULT_STATIC_THRESHOLD 0
#define DEFAULT_TOKEN_PARTITIONS 0
#define DEFAULT_ARNR_MAXFRAMES 0
#define DEFAULT_ARNR_STRENGTH 3
#define DEFAULT_ARNR_TYPE 3
#define DEFAULT_TUNING VP8_TUNE_PSNR
#define DEFAULT_CQ_LEVEL 10
#define DEFAULT_MAX_INTRA_BITRATE_PCT 0
#define DEFAULT_TIMEBASE_N 0
#define DEFAULT_TIMEBASE_D 1

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
  PROP_TIMEBASE
};

#define GST_VP9_ENC_END_USAGE_TYPE (gst_vp9_enc_end_usage_get_type())
static GType
gst_vp9_enc_end_usage_get_type (void)
{
  static const GEnumValue values[] = {
    {VPX_VBR, "Variable Bit Rate (VBR) mode", "vbr"},
    {VPX_CBR, "Constant Bit Rate (CBR) mode", "cbr"},
    {VPX_CQ, "Constant Quality Mode (CQ) mode", "cq"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVP9EncEndUsage", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VP9_ENC_MULTIPASS_MODE_TYPE (gst_vp9_enc_multipass_mode_get_type())
static GType
gst_vp9_enc_multipass_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {VPX_RC_ONE_PASS, "One pass encoding (default)", "one-pass"},
    {VPX_RC_FIRST_PASS, "First pass of multipass encoding", "first-pass"},
    {VPX_RC_LAST_PASS, "Last pass of multipass encoding", "last-pass"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVP9EncMultipassMode", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VP9_ENC_KF_MODE_TYPE (gst_vp9_enc_kf_mode_get_type())
static GType
gst_vp9_enc_kf_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {VPX_KF_AUTO, "Determine optimal placement automatically", "auto"},
    {VPX_KF_DISABLED, "Don't automatically place keyframes", "disabled"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVP9EncKfMode", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VP9_ENC_TUNING_TYPE (gst_vp9_enc_tuning_get_type())
static GType
gst_vp9_enc_tuning_get_type (void)
{
  static const GEnumValue values[] = {
    {VP8_TUNE_PSNR, "Tune for PSNR", "psnr"},
    {VP8_TUNE_SSIM, "Tune for SSIM", "ssim"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVP9EncTuning", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VP9_ENC_SCALING_MODE_TYPE (gst_vp9_enc_scaling_mode_get_type())
static GType
gst_vp9_enc_scaling_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {VP8E_NORMAL, "Normal", "normal"},
    {VP8E_FOURFIVE, "4:5", "4:5"},
    {VP8E_THREEFIVE, "3:5", "3:5"},
    {VP8E_ONETWO, "1:2", "1:2"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVP9EncScalingMode", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VP9_ENC_TOKEN_PARTITIONS_TYPE (gst_vp9_enc_token_partitions_get_type())
static GType
gst_vp9_enc_token_partitions_get_type (void)
{
  static const GEnumValue values[] = {
    {VP8_ONE_TOKENPARTITION, "One token partition", "1"},
    {VP8_TWO_TOKENPARTITION, "Two token partitions", "2"},
    {VP8_FOUR_TOKENPARTITION, "Four token partitions", "4"},
    {VP8_EIGHT_TOKENPARTITION, "Eight token partitions", "8"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstVP9EncTokenPartitions", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#define GST_VP9_ENC_ER_FLAGS_TYPE (gst_vp9_enc_er_flags_get_type())
static GType
gst_vp9_enc_er_flags_get_type (void)
{
  static const GFlagsValue values[] = {
    {VPX_ERROR_RESILIENT_DEFAULT, "Default error resilience", "default"},
    {VPX_ERROR_RESILIENT_PARTITIONS,
        "Allow partitions to be decoded independently", "partitions"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_flags_register_static ("GstVP9EncErFlags", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

static void gst_vp9_enc_finalize (GObject * object);
static void gst_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_vp9_enc_start (GstVideoEncoder * encoder);
static gboolean gst_vp9_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_vp9_enc_set_format (GstVideoEncoder *
    video_encoder, GstVideoCodecState * state);
static gboolean gst_vp9_enc_finish (GstVideoEncoder * video_encoder);
static GstFlowReturn gst_vp9_enc_handle_frame (GstVideoEncoder *
    video_encoder, GstVideoCodecFrame * frame);
static gboolean gst_vp9_enc_sink_event (GstVideoEncoder *
    video_encoder, GstEvent * event);
static gboolean gst_vp9_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

/* FIXME: Y42B and Y444 do not work yet it seems */
static GstStaticPadTemplate gst_vp9_enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    /*GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420, YV12, Y42B, Y444 }")) */
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420, YV12 }"))
    );

static GstStaticPadTemplate gst_vp9_enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9, " "profile = (string) {0, 1, 2, 3}")
    );

#define parent_class gst_vp9_enc_parent_class
G_DEFINE_TYPE_WITH_CODE (GstVP9Enc, gst_vp9_enc, GST_TYPE_VIDEO_ENCODER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL);
    G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL););

static void
gst_vp9_enc_class_init (GstVP9EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *video_encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_vp9_enc_set_property;
  gobject_class->get_property = gst_vp9_enc_get_property;
  gobject_class->finalize = gst_vp9_enc_finalize;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vp9_enc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vp9_enc_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "On2 VP9 Encoder",
      "Codec/Encoder/Video",
      "Encode VP9 video streams", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  video_encoder_class->start = gst_vp9_enc_start;
  video_encoder_class->stop = gst_vp9_enc_stop;
  video_encoder_class->handle_frame = gst_vp9_enc_handle_frame;
  video_encoder_class->set_format = gst_vp9_enc_set_format;
  video_encoder_class->finish = gst_vp9_enc_finish;
  video_encoder_class->sink_event = gst_vp9_enc_sink_event;
  video_encoder_class->propose_allocation = gst_vp9_enc_propose_allocation;

  g_object_class_install_property (gobject_class, PROP_RC_END_USAGE,
      g_param_spec_enum ("end-usage", "Rate control mode",
          "Rate control mode",
          GST_VP9_ENC_END_USAGE_TYPE, DEFAULT_RC_END_USAGE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate", "Target bitrate",
          "Target bitrate (in bits/sec)",
          0, G_MAXINT, DEFAULT_RC_TARGET_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_MIN_QUANTIZER,
      g_param_spec_int ("min-quantizer", "Minimum Quantizer",
          "Minimum Quantizer (best)",
          0, 63, DEFAULT_RC_MIN_QUANTIZER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_MAX_QUANTIZER,
      g_param_spec_int ("max-quantizer", "Maximum Quantizer",
          "Maximum Quantizer (worst)",
          0, 63, DEFAULT_RC_MAX_QUANTIZER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_DROPFRAME_THRESH,
      g_param_spec_int ("dropframe-threshold", "Drop Frame Threshold",
          "Temporal resampling threshold (buf %)",
          0, 100, DEFAULT_RC_DROPFRAME_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_RESIZE_ALLOWED,
      g_param_spec_boolean ("resize-allowed", "Resize Allowed",
          "Allow spatial resampling",
          DEFAULT_RC_RESIZE_ALLOWED,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_RESIZE_UP_THRESH,
      g_param_spec_int ("resize-up-threshold", "Resize Up Threshold",
          "Upscale threshold (buf %)",
          0, 100, DEFAULT_RC_RESIZE_UP_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_RESIZE_DOWN_THRESH,
      g_param_spec_int ("resize-down-threshold", "Resize Down Threshold",
          "Downscale threshold (buf %)",
          0, 100, DEFAULT_RC_RESIZE_DOWN_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_UNDERSHOOT_PCT,
      g_param_spec_int ("undershoot", "Undershoot PCT",
          "Datarate undershoot (min) target (%)",
          0, 1000, DEFAULT_RC_UNDERSHOOT_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_OVERSHOOT_PCT,
      g_param_spec_int ("overshoot", "Overshoot PCT",
          "Datarate overshoot (max) target (%)",
          0, 1000, DEFAULT_RC_OVERSHOOT_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_BUF_SZ,
      g_param_spec_int ("buffer-size", "Buffer size",
          "Client buffer size (ms)",
          0, G_MAXINT, DEFAULT_RC_BUF_SZ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_BUF_INITIAL_SZ,
      g_param_spec_int ("buffer-initial-size", "Buffer initial size",
          "Initial client buffer size (ms)",
          0, G_MAXINT, DEFAULT_RC_BUF_INITIAL_SZ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_BUF_OPTIMAL_SZ,
      g_param_spec_int ("buffer-optimal-size", "Buffer optimal size",
          "Optimal client buffer size (ms)",
          0, G_MAXINT, DEFAULT_RC_BUF_OPTIMAL_SZ,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_RC_2PASS_VBR_BIAS_PCT,
      g_param_spec_int ("twopass-vbr-bias", "2-pass VBR bias",
          "CBR/VBR bias (0=CBR, 100=VBR)",
          0, 100, DEFAULT_RC_2PASS_VBR_BIAS_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_RC_2PASS_VBR_MINSECTION_PCT,
      g_param_spec_int ("twopass-vbr-minsection", "2-pass GOP min bitrate",
          "GOP minimum bitrate (% target)", 0, G_MAXINT,
          DEFAULT_RC_2PASS_VBR_MINSECTION_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_RC_2PASS_VBR_MAXSECTION_PCT,
      g_param_spec_int ("twopass-vbr-maxsection", "2-pass GOP max bitrate",
          "GOP maximum bitrate (% target)", 0, G_MAXINT,
          DEFAULT_RC_2PASS_VBR_MINSECTION_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_KF_MODE,
      g_param_spec_enum ("keyframe-mode", "Keyframe Mode",
          "Keyframe placement",
          GST_VP9_ENC_KF_MODE_TYPE, DEFAULT_KF_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_KF_MAX_DIST,
      g_param_spec_int ("keyframe-max-dist", "Keyframe max distance",
          "Maximum distance between keyframes (number of frames)",
          0, G_MAXINT, DEFAULT_KF_MAX_DIST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MULTIPASS_MODE,
      g_param_spec_enum ("multipass-mode", "Multipass Mode",
          "Multipass encode mode",
          GST_VP9_ENC_MULTIPASS_MODE_TYPE, DEFAULT_MULTIPASS_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MULTIPASS_CACHE_FILE,
      g_param_spec_string ("multipass-cache-file", "Multipass Cache File",
          "Multipass cache file",
          DEFAULT_MULTIPASS_CACHE_FILE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TS_NUMBER_LAYERS,
      g_param_spec_int ("temporal-scalability-number-layers",
          "Number of coding layers", "Number of coding layers to use", 1, 5,
          DEFAULT_TS_NUMBER_LAYERS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TS_TARGET_BITRATE,
      g_param_spec_value_array ("temporal-scalability-target-bitrate",
          "Coding layer target bitrates",
          "Target bitrates for coding layers (one per layer, decreasing)",
          g_param_spec_int ("target-bitrate", "Target bitrate",
              "Target bitrate", 0, G_MAXINT, DEFAULT_RC_TARGET_BITRATE,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TS_RATE_DECIMATOR,
      g_param_spec_value_array ("temporal-scalability-rate-decimator",
          "Coding layer rate decimator",
          "Rate decimation factors for each layer",
          g_param_spec_int ("rate-decimator", "Rate decimator",
              "Rate decimator", 0, 1000000000, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TS_PERIODICITY,
      g_param_spec_int ("temporal-scalability-periodicity",
          "Coding layer periodicity",
          "Length of sequence that defines layer membership periodicity", 0, 16,
          DEFAULT_TS_PERIODICITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TS_LAYER_ID,
      g_param_spec_value_array ("temporal-scalability-layer-id",
          "Coding layer identification",
          "Sequence defining coding layer membership",
          g_param_spec_int ("layer-id", "Layer ID", "Layer ID", 0, 4, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LAG_IN_FRAMES,
      g_param_spec_int ("lag-in-frames", "Lag in frames",
          "Maximum number of frames to lag",
          0, 25, DEFAULT_LAG_IN_FRAMES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ERROR_RESILIENT,
      g_param_spec_flags ("error-resilient", "Error resilient",
          "Error resilience flags",
          GST_VP9_ENC_ER_FLAGS_TYPE, DEFAULT_ERROR_RESILIENT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_THREADS,
      g_param_spec_int ("threads", "Threads",
          "Number of threads to use",
          0, 64, DEFAULT_THREADS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEADLINE,
      g_param_spec_int64 ("deadline", "Deadline",
          "Deadline per frame (usec, 0=disabled)",
          0, G_MAXINT64, DEFAULT_DEADLINE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_H_SCALING_MODE,
      g_param_spec_enum ("horizontal-scaling-mode", "Horizontal scaling mode",
          "Horizontal scaling mode",
          GST_VP9_ENC_SCALING_MODE_TYPE, DEFAULT_H_SCALING_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_V_SCALING_MODE,
      g_param_spec_enum ("vertical-scaling-mode", "Vertical scaling mode",
          "Vertical scaling mode",
          GST_VP9_ENC_SCALING_MODE_TYPE, DEFAULT_V_SCALING_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CPU_USED,
      g_param_spec_int ("cpu-used", "CPU used",
          "CPU used",
          -16, 16, DEFAULT_CPU_USED,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ENABLE_AUTO_ALT_REF,
      g_param_spec_boolean ("auto-alt-ref", "Auto alt reference frames",
          "Automatically generate AltRef frames",
          DEFAULT_ENABLE_AUTO_ALT_REF,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NOISE_SENSITIVITY,
      g_param_spec_int ("noise-sensitivity", "Noise sensitivity",
          "Noise sensisivity (frames to blur)",
          0, 6, DEFAULT_NOISE_SENSITIVITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SHARPNESS,
      g_param_spec_int ("sharpness", "Sharpness",
          "Filter sharpness",
          0, 7, DEFAULT_SHARPNESS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_STATIC_THRESHOLD,
      g_param_spec_int ("static-threshold", "Static Threshold",
          "Motion detection threshold",
          0, G_MAXINT, DEFAULT_STATIC_THRESHOLD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TOKEN_PARTITIONS,
      g_param_spec_enum ("token-partitions", "Token partitions",
          "Number of token partitions",
          GST_VP9_ENC_TOKEN_PARTITIONS_TYPE, DEFAULT_TOKEN_PARTITIONS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ARNR_MAXFRAMES,
      g_param_spec_int ("arnr-maxframes", "AltRef max frames",
          "AltRef maximum number of frames",
          0, 15, DEFAULT_ARNR_MAXFRAMES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ARNR_STRENGTH,
      g_param_spec_int ("arnr-strength", "AltRef strength",
          "AltRef strength",
          0, 6, DEFAULT_ARNR_STRENGTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ARNR_TYPE,
      g_param_spec_int ("arnr-type", "AltRef type",
          "AltRef type",
          1, 3, DEFAULT_ARNR_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TUNING,
      g_param_spec_enum ("tuning", "Tuning",
          "Tuning",
          GST_VP9_ENC_TUNING_TYPE, DEFAULT_TUNING,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CQ_LEVEL,
      g_param_spec_int ("cq-level", "Constrained quality level",
          "Constrained quality level",
          0, 63, DEFAULT_CQ_LEVEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MAX_INTRA_BITRATE_PCT,
      g_param_spec_int ("max-intra-bitrate", "Max Intra bitrate",
          "Maximum Intra frame bitrate",
          0, G_MAXINT, DEFAULT_MAX_INTRA_BITRATE_PCT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TIMEBASE,
      gst_param_spec_fraction ("timebase", "Shortest interframe time",
          "Fraction of one second that is the shortest interframe time - normally left as zero which will default to the framerate",
          0, 1, G_MAXINT, 1, DEFAULT_TIMEBASE_N, DEFAULT_TIMEBASE_D,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_vp9enc_debug, "vp9enc", 0, "VP9 Encoder");
}

static void
gst_vp9_enc_init (GstVP9Enc * gst_vp9_enc)
{
  vpx_codec_err_t status;

  GST_DEBUG_OBJECT (gst_vp9_enc, "init");

  status =
      vpx_codec_enc_config_default (&vpx_codec_vp9_cx_algo, &gst_vp9_enc->cfg,
      0);
  if (status != VPX_CODEC_OK) {
    GST_ERROR_OBJECT (gst_vp9_enc,
        "Failed to get default encoder configuration: %s",
        gst_vpx_error_name (status));
    gst_vp9_enc->have_default_config = FALSE;
  } else {
    gst_vp9_enc->have_default_config = TRUE;
  }

  gst_vp9_enc->cfg.rc_end_usage = DEFAULT_RC_END_USAGE;
  gst_vp9_enc->cfg.rc_target_bitrate = DEFAULT_RC_TARGET_BITRATE / 1000;
  gst_vp9_enc->rc_target_bitrate_set = FALSE;
  gst_vp9_enc->cfg.rc_min_quantizer = DEFAULT_RC_MIN_QUANTIZER;
  gst_vp9_enc->cfg.rc_max_quantizer = DEFAULT_RC_MAX_QUANTIZER;
  gst_vp9_enc->cfg.rc_dropframe_thresh = DEFAULT_RC_DROPFRAME_THRESH;
  gst_vp9_enc->cfg.rc_resize_allowed = DEFAULT_RC_RESIZE_ALLOWED;
  gst_vp9_enc->cfg.rc_resize_up_thresh = DEFAULT_RC_RESIZE_UP_THRESH;
  gst_vp9_enc->cfg.rc_resize_down_thresh = DEFAULT_RC_RESIZE_DOWN_THRESH;
  gst_vp9_enc->cfg.rc_undershoot_pct = DEFAULT_RC_UNDERSHOOT_PCT;
  gst_vp9_enc->cfg.rc_overshoot_pct = DEFAULT_RC_OVERSHOOT_PCT;
  gst_vp9_enc->cfg.rc_buf_sz = DEFAULT_RC_BUF_SZ;
  gst_vp9_enc->cfg.rc_buf_initial_sz = DEFAULT_RC_BUF_INITIAL_SZ;
  gst_vp9_enc->cfg.rc_buf_optimal_sz = DEFAULT_RC_BUF_OPTIMAL_SZ;
  gst_vp9_enc->cfg.rc_2pass_vbr_bias_pct = DEFAULT_RC_2PASS_VBR_BIAS_PCT;
  gst_vp9_enc->cfg.rc_2pass_vbr_minsection_pct =
      DEFAULT_RC_2PASS_VBR_MINSECTION_PCT;
  gst_vp9_enc->cfg.rc_2pass_vbr_maxsection_pct =
      DEFAULT_RC_2PASS_VBR_MAXSECTION_PCT;
  gst_vp9_enc->cfg.kf_mode = DEFAULT_KF_MODE;
  gst_vp9_enc->cfg.kf_max_dist = DEFAULT_KF_MAX_DIST;
  gst_vp9_enc->cfg.g_pass = DEFAULT_MULTIPASS_MODE;
  gst_vp9_enc->multipass_cache_file = g_strdup (DEFAULT_MULTIPASS_CACHE_FILE);
  gst_vp9_enc->cfg.ts_number_layers = DEFAULT_TS_NUMBER_LAYERS;
  gst_vp9_enc->n_ts_target_bitrate = 0;
  gst_vp9_enc->n_ts_rate_decimator = 0;
  gst_vp9_enc->cfg.ts_periodicity = DEFAULT_TS_PERIODICITY;
  gst_vp9_enc->n_ts_layer_id = 0;
  gst_vp9_enc->cfg.g_error_resilient = DEFAULT_ERROR_RESILIENT;
  gst_vp9_enc->cfg.g_lag_in_frames = DEFAULT_LAG_IN_FRAMES;
  gst_vp9_enc->cfg.g_threads = DEFAULT_THREADS;
  gst_vp9_enc->deadline = DEFAULT_DEADLINE;
  gst_vp9_enc->h_scaling_mode = DEFAULT_H_SCALING_MODE;
  gst_vp9_enc->v_scaling_mode = DEFAULT_V_SCALING_MODE;
  gst_vp9_enc->cpu_used = DEFAULT_CPU_USED;
  gst_vp9_enc->enable_auto_alt_ref = DEFAULT_ENABLE_AUTO_ALT_REF;
  gst_vp9_enc->noise_sensitivity = DEFAULT_NOISE_SENSITIVITY;
  gst_vp9_enc->sharpness = DEFAULT_SHARPNESS;
  gst_vp9_enc->static_threshold = DEFAULT_STATIC_THRESHOLD;
  gst_vp9_enc->token_partitions = DEFAULT_TOKEN_PARTITIONS;
  gst_vp9_enc->arnr_maxframes = DEFAULT_ARNR_MAXFRAMES;
  gst_vp9_enc->arnr_strength = DEFAULT_ARNR_STRENGTH;
  gst_vp9_enc->arnr_type = DEFAULT_ARNR_TYPE;
  gst_vp9_enc->tuning = DEFAULT_TUNING;
  gst_vp9_enc->cq_level = DEFAULT_CQ_LEVEL;
  gst_vp9_enc->max_intra_bitrate_pct = DEFAULT_MAX_INTRA_BITRATE_PCT;
  gst_vp9_enc->timebase_n = DEFAULT_TIMEBASE_N;
  gst_vp9_enc->timebase_d = DEFAULT_TIMEBASE_D;

  gst_vp9_enc->cfg.g_profile = DEFAULT_PROFILE;

  g_mutex_init (&gst_vp9_enc->encoder_lock);
}

static void
gst_vp9_enc_finalize (GObject * object)
{
  GstVP9Enc *gst_vp9_enc;

  GST_DEBUG_OBJECT (object, "finalize");

  g_return_if_fail (GST_IS_VP9_ENC (object));
  gst_vp9_enc = GST_VP9_ENC (object);

  g_free (gst_vp9_enc->multipass_cache_file);
  gst_vp9_enc->multipass_cache_file = NULL;

  if (gst_vp9_enc->input_state)
    gst_video_codec_state_unref (gst_vp9_enc->input_state);

  g_mutex_clear (&gst_vp9_enc->encoder_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);

}

static void
gst_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVP9Enc *gst_vp9_enc;
  gboolean global = FALSE;
  vpx_codec_err_t status;

  g_return_if_fail (GST_IS_VP9_ENC (object));
  gst_vp9_enc = GST_VP9_ENC (object);

  GST_DEBUG_OBJECT (object, "gst_vp9_enc_set_property");
  g_mutex_lock (&gst_vp9_enc->encoder_lock);
  switch (prop_id) {
    case PROP_RC_END_USAGE:
      gst_vp9_enc->cfg.rc_end_usage = g_value_get_enum (value);
      global = TRUE;
      break;
    case PROP_RC_TARGET_BITRATE:
      gst_vp9_enc->cfg.rc_target_bitrate = g_value_get_int (value) / 1000;
      gst_vp9_enc->rc_target_bitrate_set = TRUE;
      global = TRUE;
      break;
    case PROP_RC_MIN_QUANTIZER:
      gst_vp9_enc->cfg.rc_min_quantizer = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_MAX_QUANTIZER:
      gst_vp9_enc->cfg.rc_max_quantizer = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_DROPFRAME_THRESH:
      gst_vp9_enc->cfg.rc_dropframe_thresh = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_RESIZE_ALLOWED:
      gst_vp9_enc->cfg.rc_resize_allowed = g_value_get_boolean (value);
      global = TRUE;
      break;
    case PROP_RC_RESIZE_UP_THRESH:
      gst_vp9_enc->cfg.rc_resize_up_thresh = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_RESIZE_DOWN_THRESH:
      gst_vp9_enc->cfg.rc_resize_down_thresh = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_UNDERSHOOT_PCT:
      gst_vp9_enc->cfg.rc_undershoot_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_OVERSHOOT_PCT:
      gst_vp9_enc->cfg.rc_overshoot_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_BUF_SZ:
      gst_vp9_enc->cfg.rc_buf_sz = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_BUF_INITIAL_SZ:
      gst_vp9_enc->cfg.rc_buf_initial_sz = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_BUF_OPTIMAL_SZ:
      gst_vp9_enc->cfg.rc_buf_optimal_sz = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_2PASS_VBR_BIAS_PCT:
      gst_vp9_enc->cfg.rc_2pass_vbr_bias_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_2PASS_VBR_MINSECTION_PCT:
      gst_vp9_enc->cfg.rc_2pass_vbr_minsection_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_RC_2PASS_VBR_MAXSECTION_PCT:
      gst_vp9_enc->cfg.rc_2pass_vbr_maxsection_pct = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_KF_MODE:
      gst_vp9_enc->cfg.kf_mode = g_value_get_enum (value);
      global = TRUE;
      break;
    case PROP_KF_MAX_DIST:
      gst_vp9_enc->cfg.kf_max_dist = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_MULTIPASS_MODE:
      gst_vp9_enc->cfg.g_pass = g_value_get_enum (value);
      global = TRUE;
      break;
    case PROP_MULTIPASS_CACHE_FILE:
      if (gst_vp9_enc->multipass_cache_file)
        g_free (gst_vp9_enc->multipass_cache_file);
      gst_vp9_enc->multipass_cache_file = g_value_dup_string (value);
      break;
    case PROP_TS_NUMBER_LAYERS:
      gst_vp9_enc->cfg.ts_number_layers = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_TS_TARGET_BITRATE:{
      GValueArray *va = g_value_get_boxed (value);

      memset (&gst_vp9_enc->cfg.ts_target_bitrate, 0,
          sizeof (gst_vp9_enc->cfg.ts_target_bitrate));
      if (va == NULL) {
        gst_vp9_enc->n_ts_target_bitrate = 0;
      } else {
        if (va->n_values > VPX_TS_MAX_LAYERS) {
          g_warning ("%s: Only %d layers allowed at maximum",
              GST_ELEMENT_NAME (gst_vp9_enc), VPX_TS_MAX_LAYERS);
        } else {
          gint i;

          for (i = 0; i < va->n_values; i++)
            gst_vp9_enc->cfg.ts_target_bitrate[i] =
                g_value_get_int (g_value_array_get_nth (va, i));
          gst_vp9_enc->n_ts_target_bitrate = va->n_values;
        }
      }
      global = TRUE;
      break;
    }
    case PROP_TS_RATE_DECIMATOR:{
      GValueArray *va = g_value_get_boxed (value);

      memset (&gst_vp9_enc->cfg.ts_rate_decimator, 0,
          sizeof (gst_vp9_enc->cfg.ts_rate_decimator));
      if (va == NULL) {
        gst_vp9_enc->n_ts_rate_decimator = 0;
      } else if (va->n_values > VPX_TS_MAX_LAYERS) {
        g_warning ("%s: Only %d layers allowed at maximum",
            GST_ELEMENT_NAME (gst_vp9_enc), VPX_TS_MAX_LAYERS);
      } else {
        gint i;

        for (i = 0; i < va->n_values; i++)
          gst_vp9_enc->cfg.ts_rate_decimator[i] =
              g_value_get_int (g_value_array_get_nth (va, i));
        gst_vp9_enc->n_ts_rate_decimator = va->n_values;
      }
      global = TRUE;
      break;
    }
    case PROP_TS_PERIODICITY:
      gst_vp9_enc->cfg.ts_periodicity = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_TS_LAYER_ID:{
      GValueArray *va = g_value_get_boxed (value);

      memset (&gst_vp9_enc->cfg.ts_layer_id, 0,
          sizeof (gst_vp9_enc->cfg.ts_layer_id));
      if (va && va->n_values > VPX_TS_MAX_PERIODICITY) {
        g_warning ("%s: Only %d sized layer sequences allowed at maximum",
            GST_ELEMENT_NAME (gst_vp9_enc), VPX_TS_MAX_PERIODICITY);
      } else if (va) {
        gint i;

        for (i = 0; i < va->n_values; i++)
          gst_vp9_enc->cfg.ts_layer_id[i] =
              g_value_get_int (g_value_array_get_nth (va, i));
        gst_vp9_enc->n_ts_layer_id = va->n_values;
      } else {
        gst_vp9_enc->n_ts_layer_id = 0;
      }
      global = TRUE;
      break;
    }
    case PROP_ERROR_RESILIENT:
      gst_vp9_enc->cfg.g_error_resilient = g_value_get_flags (value);
      global = TRUE;
      break;
    case PROP_LAG_IN_FRAMES:
      gst_vp9_enc->cfg.g_lag_in_frames = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_THREADS:
      gst_vp9_enc->cfg.g_threads = g_value_get_int (value);
      global = TRUE;
      break;
    case PROP_DEADLINE:
      gst_vp9_enc->deadline = g_value_get_int64 (value);
      break;
    case PROP_H_SCALING_MODE:
      gst_vp9_enc->h_scaling_mode = g_value_get_enum (value);
      if (gst_vp9_enc->inited) {
        vpx_scaling_mode_t sm;

        sm.h_scaling_mode = gst_vp9_enc->h_scaling_mode;
        sm.v_scaling_mode = gst_vp9_enc->v_scaling_mode;

        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_SCALEMODE, &sm);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_SCALEMODE: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_V_SCALING_MODE:
      gst_vp9_enc->v_scaling_mode = g_value_get_enum (value);
      if (gst_vp9_enc->inited) {
        vpx_scaling_mode_t sm;

        sm.h_scaling_mode = gst_vp9_enc->h_scaling_mode;
        sm.v_scaling_mode = gst_vp9_enc->v_scaling_mode;

        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_SCALEMODE, &sm);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_SCALEMODE: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_CPU_USED:
      gst_vp9_enc->cpu_used = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_CPUUSED,
            gst_vp9_enc->cpu_used);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc, "Failed to set VP8E_SET_CPUUSED: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_ENABLE_AUTO_ALT_REF:
      gst_vp9_enc->enable_auto_alt_ref = g_value_get_boolean (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_ENABLEAUTOALTREF,
            (gst_vp9_enc->enable_auto_alt_ref ? 1 : 0));
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_ENABLEAUTOALTREF: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_NOISE_SENSITIVITY:
      gst_vp9_enc->noise_sensitivity = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder,
            VP8E_SET_NOISE_SENSITIVITY, gst_vp9_enc->noise_sensitivity);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_NOISE_SENSITIVITY: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_SHARPNESS:
      gst_vp9_enc->sharpness = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status = vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_SHARPNESS,
            gst_vp9_enc->sharpness);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_SHARPNESS: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_STATIC_THRESHOLD:
      gst_vp9_enc->static_threshold = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_STATIC_THRESHOLD,
            gst_vp9_enc->static_threshold);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_STATIC_THRESHOLD: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_TOKEN_PARTITIONS:
      gst_vp9_enc->token_partitions = g_value_get_enum (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_TOKEN_PARTITIONS,
            gst_vp9_enc->token_partitions);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_TOKEN_PARTIONS: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_ARNR_MAXFRAMES:
      gst_vp9_enc->arnr_maxframes = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_ARNR_MAXFRAMES,
            gst_vp9_enc->arnr_maxframes);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_ARNR_MAXFRAMES: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_ARNR_STRENGTH:
      gst_vp9_enc->arnr_strength = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_ARNR_STRENGTH,
            gst_vp9_enc->arnr_strength);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_ARNR_STRENGTH: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_ARNR_TYPE:
      gst_vp9_enc->arnr_type = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status = vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_ARNR_TYPE,
            gst_vp9_enc->arnr_type);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_ARNR_TYPE: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_TUNING:
      gst_vp9_enc->tuning = g_value_get_enum (value);
      if (gst_vp9_enc->inited) {
        status = vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_TUNING,
            gst_vp9_enc->tuning);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_TUNING: %s", gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_CQ_LEVEL:
      gst_vp9_enc->cq_level = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status = vpx_codec_control (&gst_vp9_enc->encoder, VP8E_SET_CQ_LEVEL,
            gst_vp9_enc->cq_level);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_CQ_LEVEL: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_MAX_INTRA_BITRATE_PCT:
      gst_vp9_enc->max_intra_bitrate_pct = g_value_get_int (value);
      if (gst_vp9_enc->inited) {
        status =
            vpx_codec_control (&gst_vp9_enc->encoder,
            VP8E_SET_MAX_INTRA_BITRATE_PCT, gst_vp9_enc->max_intra_bitrate_pct);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vp9_enc,
              "Failed to set VP8E_SET_MAX_INTRA_BITRATE_PCT: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_TIMEBASE:
      gst_vp9_enc->timebase_n = gst_value_get_fraction_numerator (value);
      gst_vp9_enc->timebase_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      break;
  }

  if (global &&gst_vp9_enc->inited) {
    status =
        vpx_codec_enc_config_set (&gst_vp9_enc->encoder, &gst_vp9_enc->cfg);
    if (status != VPX_CODEC_OK) {
      g_mutex_unlock (&gst_vp9_enc->encoder_lock);
      GST_ELEMENT_ERROR (gst_vp9_enc, LIBRARY, INIT,
          ("Failed to set encoder configuration"), ("%s",
              gst_vpx_error_name (status)));
    } else {
      g_mutex_unlock (&gst_vp9_enc->encoder_lock);
    }
  } else {
    g_mutex_unlock (&gst_vp9_enc->encoder_lock);
  }
}

static void
gst_vp9_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVP9Enc *gst_vp9_enc;

  g_return_if_fail (GST_IS_VP9_ENC (object));
  gst_vp9_enc = GST_VP9_ENC (object);

  g_mutex_lock (&gst_vp9_enc->encoder_lock);
  switch (prop_id) {
    case PROP_RC_END_USAGE:
      g_value_set_enum (value, gst_vp9_enc->cfg.rc_end_usage);
      break;
    case PROP_RC_TARGET_BITRATE:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_target_bitrate * 1000);
      break;
    case PROP_RC_MIN_QUANTIZER:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_min_quantizer);
      break;
    case PROP_RC_MAX_QUANTIZER:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_max_quantizer);
      break;
    case PROP_RC_DROPFRAME_THRESH:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_dropframe_thresh);
      break;
    case PROP_RC_RESIZE_ALLOWED:
      g_value_set_boolean (value, gst_vp9_enc->cfg.rc_resize_allowed);
      break;
    case PROP_RC_RESIZE_UP_THRESH:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_resize_up_thresh);
      break;
    case PROP_RC_RESIZE_DOWN_THRESH:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_resize_down_thresh);
      break;
    case PROP_RC_UNDERSHOOT_PCT:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_undershoot_pct);
      break;
    case PROP_RC_OVERSHOOT_PCT:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_overshoot_pct);
      break;
    case PROP_RC_BUF_SZ:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_buf_sz);
      break;
    case PROP_RC_BUF_INITIAL_SZ:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_buf_initial_sz);
      break;
    case PROP_RC_BUF_OPTIMAL_SZ:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_buf_optimal_sz);
      break;
    case PROP_RC_2PASS_VBR_BIAS_PCT:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_2pass_vbr_bias_pct);
      break;
    case PROP_RC_2PASS_VBR_MINSECTION_PCT:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_2pass_vbr_minsection_pct);
      break;
    case PROP_RC_2PASS_VBR_MAXSECTION_PCT:
      g_value_set_int (value, gst_vp9_enc->cfg.rc_2pass_vbr_maxsection_pct);
      break;
    case PROP_KF_MODE:
      g_value_set_enum (value, gst_vp9_enc->cfg.kf_mode);
      break;
    case PROP_KF_MAX_DIST:
      g_value_set_int (value, gst_vp9_enc->cfg.kf_max_dist);
      break;
    case PROP_MULTIPASS_MODE:
      g_value_set_enum (value, gst_vp9_enc->cfg.g_pass);
      break;
    case PROP_MULTIPASS_CACHE_FILE:
      g_value_set_string (value, gst_vp9_enc->multipass_cache_file);
      break;
    case PROP_TS_NUMBER_LAYERS:
      g_value_set_int (value, gst_vp9_enc->cfg.ts_number_layers);
      break;
    case PROP_TS_TARGET_BITRATE:{
      GValueArray *va;

      if (gst_vp9_enc->n_ts_target_bitrate == 0) {
        g_value_set_boxed (value, NULL);
      } else {
        gint i;

        va = g_value_array_new (gst_vp9_enc->n_ts_target_bitrate);
        for (i = 0; i < gst_vp9_enc->n_ts_target_bitrate; i++) {
          GValue v = { 0, };

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, gst_vp9_enc->cfg.ts_target_bitrate[i]);
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

      if (gst_vp9_enc->n_ts_rate_decimator == 0) {
        g_value_set_boxed (value, NULL);
      } else {
        gint i;

        va = g_value_array_new (gst_vp9_enc->n_ts_rate_decimator);
        for (i = 0; i < gst_vp9_enc->n_ts_rate_decimator; i++) {
          GValue v = { 0, };

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, gst_vp9_enc->cfg.ts_rate_decimator[i]);
          g_value_array_append (va, &v);
          g_value_unset (&v);
        }
        g_value_set_boxed (value, va);
        g_value_array_free (va);
      }
      break;
    }
    case PROP_TS_PERIODICITY:
      g_value_set_int (value, gst_vp9_enc->cfg.ts_periodicity);
      break;
    case PROP_TS_LAYER_ID:{
      GValueArray *va;

      if (gst_vp9_enc->n_ts_layer_id == 0) {
        g_value_set_boxed (value, NULL);
      } else {
        gint i;

        va = g_value_array_new (gst_vp9_enc->n_ts_layer_id);
        for (i = 0; i < gst_vp9_enc->n_ts_layer_id; i++) {
          GValue v = { 0, };

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, gst_vp9_enc->cfg.ts_layer_id[i]);
          g_value_array_append (va, &v);
          g_value_unset (&v);
        }
        g_value_set_boxed (value, va);
        g_value_array_free (va);
      }
      break;
    }
    case PROP_ERROR_RESILIENT:
      g_value_set_flags (value, gst_vp9_enc->cfg.g_error_resilient);
      break;
    case PROP_LAG_IN_FRAMES:
      g_value_set_int (value, gst_vp9_enc->cfg.g_lag_in_frames);
      break;
    case PROP_THREADS:
      g_value_set_int (value, gst_vp9_enc->cfg.g_threads);
      break;
    case PROP_DEADLINE:
      g_value_set_int64 (value, gst_vp9_enc->deadline);
      break;
    case PROP_H_SCALING_MODE:
      g_value_set_enum (value, gst_vp9_enc->h_scaling_mode);
      break;
    case PROP_V_SCALING_MODE:
      g_value_set_enum (value, gst_vp9_enc->v_scaling_mode);
      break;
    case PROP_CPU_USED:
      g_value_set_int (value, gst_vp9_enc->cpu_used);
      break;
    case PROP_ENABLE_AUTO_ALT_REF:
      g_value_set_boolean (value, gst_vp9_enc->enable_auto_alt_ref);
      break;
    case PROP_NOISE_SENSITIVITY:
      g_value_set_int (value, gst_vp9_enc->noise_sensitivity);
      break;
    case PROP_SHARPNESS:
      g_value_set_int (value, gst_vp9_enc->sharpness);
      break;
    case PROP_STATIC_THRESHOLD:
      g_value_set_int (value, gst_vp9_enc->static_threshold);
      break;
    case PROP_TOKEN_PARTITIONS:
      g_value_set_enum (value, gst_vp9_enc->token_partitions);
      break;
    case PROP_ARNR_MAXFRAMES:
      g_value_set_int (value, gst_vp9_enc->arnr_maxframes);
      break;
    case PROP_ARNR_STRENGTH:
      g_value_set_int (value, gst_vp9_enc->arnr_strength);
      break;
    case PROP_ARNR_TYPE:
      g_value_set_int (value, gst_vp9_enc->arnr_type);
      break;
    case PROP_TUNING:
      g_value_set_enum (value, gst_vp9_enc->tuning);
      break;
    case PROP_CQ_LEVEL:
      g_value_set_int (value, gst_vp9_enc->cq_level);
      break;
    case PROP_MAX_INTRA_BITRATE_PCT:
      g_value_set_int (value, gst_vp9_enc->max_intra_bitrate_pct);
      break;
    case PROP_TIMEBASE:
      gst_value_set_fraction (value, gst_vp9_enc->timebase_n,
          gst_vp9_enc->timebase_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&gst_vp9_enc->encoder_lock);
}

static gboolean
gst_vp9_enc_start (GstVideoEncoder * video_encoder)
{
  GstVP9Enc *encoder = GST_VP9_ENC (video_encoder);

  GST_DEBUG_OBJECT (video_encoder, "start");

  if (!encoder->have_default_config) {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT,
        ("Failed to get default encoder configuration"), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vp9_enc_stop (GstVideoEncoder * video_encoder)
{
  GstVP9Enc *encoder;

  GST_DEBUG_OBJECT (video_encoder, "stop");

  encoder = GST_VP9_ENC (video_encoder);

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
  g_mutex_unlock (&encoder->encoder_lock);

  gst_tag_setter_reset_tags (GST_TAG_SETTER (encoder));

  return TRUE;
}

static gint
gst_vp9_enc_get_downstream_profile (GstVP9Enc * encoder)
{
  GstCaps *allowed;
  GstStructure *s;
  gint profile = DEFAULT_PROFILE;

  allowed = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (allowed) {
    allowed = gst_caps_truncate (allowed);
    s = gst_caps_get_structure (allowed, 0);
    if (gst_structure_has_field (s, "profile")) {
      const GValue *v = gst_structure_get_value (s, "profile");
      const gchar *profile_str = NULL;

      if (GST_VALUE_HOLDS_LIST (v) && gst_value_list_get_size (v) > 0) {
        profile_str = g_value_get_string (gst_value_list_get_value (v, 0));
      } else if (G_VALUE_HOLDS_STRING (v)) {
        profile_str = g_value_get_string (v);
      }

      if (profile_str) {
        gchar *endptr = NULL;

        profile = g_ascii_strtoull (profile_str, &endptr, 10);
        if (*endptr != '\0' || profile < 0 || profile > 3) {
          GST_ERROR_OBJECT (encoder, "Invalid profile '%s'", profile_str);
          profile = DEFAULT_PROFILE;
        }
      }
    }
    gst_caps_unref (allowed);
  }

  GST_DEBUG_OBJECT (encoder, "Using profile %d", profile);

  return profile;
}

static gboolean
gst_vp9_enc_set_format (GstVideoEncoder * video_encoder,
    GstVideoCodecState * state)
{
  GstVP9Enc *encoder;
  vpx_codec_err_t status;
  vpx_image_t *image;
  GstCaps *caps;
  gboolean ret = TRUE;
  GstVideoInfo *info = &state->info;
  GstVideoCodecState *output_state;
  gchar *profile_str;

  encoder = GST_VP9_ENC (video_encoder);
  GST_DEBUG_OBJECT (video_encoder, "set_format");

  if (encoder->inited) {
    GST_DEBUG_OBJECT (video_encoder, "refusing renegotiation");
    return FALSE;
  }

  g_mutex_lock (&encoder->encoder_lock);
  encoder->cfg.g_profile = gst_vp9_enc_get_downstream_profile (encoder);

  /* Scale default bitrate to our size */
  if (!encoder->rc_target_bitrate_set)
    encoder->cfg.rc_target_bitrate =
        gst_util_uint64_scale (DEFAULT_RC_TARGET_BITRATE,
        GST_VIDEO_INFO_WIDTH (info) * GST_VIDEO_INFO_HEIGHT (info),
        320 * 240 * 1000);

  encoder->cfg.g_w = GST_VIDEO_INFO_WIDTH (info);
  encoder->cfg.g_h = GST_VIDEO_INFO_HEIGHT (info);

  if (encoder->timebase_n != 0 && encoder->timebase_d != 0) {
    GST_DEBUG_OBJECT (video_encoder, "Using timebase configuration");
    encoder->cfg.g_timebase.num = encoder->timebase_n;
    encoder->cfg.g_timebase.den = encoder->timebase_d;
  } else if (GST_VIDEO_INFO_FPS_D (info) != 0
      && GST_VIDEO_INFO_FPS_N (info) != 0) {
    /* GstVideoInfo holds either the framerate or max-framerate (if framerate
     * is 0) in FPS so this will be used if max-framerate or framerate
     * is set */
    GST_DEBUG_OBJECT (video_encoder, "Setting timebase from framerate");
    encoder->cfg.g_timebase.num = GST_VIDEO_INFO_FPS_D (info);
    encoder->cfg.g_timebase.den = GST_VIDEO_INFO_FPS_N (info);
  } else {
    /* Zero framerate and max-framerate but still need to setup the timebase to avoid
     * a divide by zero error. Presuming the lowest common denominator will be RTP -
     * VP9 payload draft states clock rate of 90000 which should work for anyone where
     * FPS < 90000 (shouldn't be too many cases where it's higher) though wouldn't be optimal. RTP specification
     * http://tools.ietf.org/html/draft-ietf-payload-vp9-01 section 6.3.1 */
    GST_WARNING_OBJECT (encoder,
        "No timebase and zero framerate setting timebase to 1/90000");
    encoder->cfg.g_timebase.num = 1;
    encoder->cfg.g_timebase.den = 90000;
  }

  if (encoder->cfg.g_pass == VPX_RC_FIRST_PASS) {
    encoder->first_pass_cache_content = g_byte_array_sized_new (4096);
  } else if (encoder->cfg.g_pass == VPX_RC_LAST_PASS) {
    GError *err = NULL;

    if (!encoder->multipass_cache_file) {
      GST_ELEMENT_ERROR (encoder, RESOURCE, OPEN_READ,
          ("No multipass cache file provided"), (NULL));
      g_mutex_unlock (&encoder->encoder_lock);
      return FALSE;
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

  status = vpx_codec_enc_init (&encoder->encoder, &vpx_codec_vp9_cx_algo,
      &encoder->cfg, 0);
  if (status != VPX_CODEC_OK) {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT,
        ("Failed to initialize encoder"), ("%s", gst_vpx_error_name (status)));
    g_mutex_unlock (&encoder->encoder_lock);
    return FALSE;
  }

  /* FIXME: Disabled for now, does not work with VP9 */
#if 0
  {
    vpx_scaling_mode_t sm;

    sm.h_scaling_mode = encoder->h_scaling_mode;
    sm.v_scaling_mode = encoder->v_scaling_mode;

    status = vpx_codec_control (&encoder->encoder, VP8E_SET_SCALEMODE, &sm);
    if (status != VPX_CODEC_OK) {
      GST_WARNING_OBJECT (encoder, "Failed to set VP8E_SET_SCALEMODE: %s",
          gst_vpx_error_name (status));
    }
  }
#endif

  status =
      vpx_codec_control (&encoder->encoder, VP8E_SET_CPUUSED,
      encoder->cpu_used);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder, "Failed to set VP8E_SET_CPUUSED: %s",
        gst_vpx_error_name (status));
  }

  status =
      vpx_codec_control (&encoder->encoder, VP8E_SET_ENABLEAUTOALTREF,
      (encoder->enable_auto_alt_ref ? 1 : 0));
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_ENABLEAUTOALTREF: %s",
        gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_NOISE_SENSITIVITY,
      encoder->noise_sensitivity);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_NOISE_SENSITIVITY: %s",
        gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_SHARPNESS,
      encoder->sharpness);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_SHARPNESS: %s", gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_STATIC_THRESHOLD,
      encoder->static_threshold);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_STATIC_THRESHOLD: %s",
        gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_TOKEN_PARTITIONS,
      encoder->token_partitions);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_TOKEN_PARTIONS: %s",
        gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_ARNR_MAXFRAMES,
      encoder->arnr_maxframes);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_ARNR_MAXFRAMES: %s",
        gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_ARNR_STRENGTH,
      encoder->arnr_strength);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_ARNR_STRENGTH: %s",
        gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_ARNR_TYPE,
      encoder->arnr_type);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_ARNR_TYPE: %s", gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_TUNING,
      encoder->tuning);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_TUNING: %s", gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_CQ_LEVEL,
      encoder->cq_level);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_CQ_LEVEL: %s", gst_vpx_error_name (status));
  }
  status = vpx_codec_control (&encoder->encoder, VP8E_SET_MAX_INTRA_BITRATE_PCT,
      encoder->max_intra_bitrate_pct);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP8E_SET_MAX_INTRA_BITRATE_PCT: %s",
        gst_vpx_error_name (status));
  }

  if (GST_VIDEO_INFO_FPS_D (info) == 0 || GST_VIDEO_INFO_FPS_N (info) == 0) {
    gst_video_encoder_set_latency (video_encoder, GST_CLOCK_TIME_NONE,
        GST_CLOCK_TIME_NONE);
  } else {
    gst_video_encoder_set_latency (video_encoder, 0,
        gst_util_uint64_scale (encoder->cfg.g_lag_in_frames,
            GST_VIDEO_INFO_FPS_D (info) * GST_SECOND,
            GST_VIDEO_INFO_FPS_N (info)));
  }
  encoder->inited = TRUE;

  /* Store input state */
  if (encoder->input_state)
    gst_video_codec_state_unref (encoder->input_state);
  encoder->input_state = gst_video_codec_state_ref (state);

  /* prepare cached image buffer setup */
  image = &encoder->image;
  memset (image, 0, sizeof (*image));

  switch (encoder->input_state->info.finfo->format) {
    case GST_VIDEO_FORMAT_I420:
      image->fmt = VPX_IMG_FMT_I420;
      image->bps = 12;
      image->x_chroma_shift = image->y_chroma_shift = 1;
      break;
    case GST_VIDEO_FORMAT_YV12:
      image->fmt = VPX_IMG_FMT_YV12;
      image->bps = 12;
      image->x_chroma_shift = image->y_chroma_shift = 1;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      image->fmt = VPX_IMG_FMT_I422;
      image->bps = 16;
      image->x_chroma_shift = 1;
      image->y_chroma_shift = 0;
      break;
    case GST_VIDEO_FORMAT_Y444:
      image->fmt = VPX_IMG_FMT_I444;
      image->bps = 24;
      image->x_chroma_shift = image->y_chroma_shift = 0;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  image->w = image->d_w = GST_VIDEO_INFO_WIDTH (info);
  image->h = image->d_h = GST_VIDEO_INFO_HEIGHT (info);

  image->stride[VPX_PLANE_Y] = GST_VIDEO_INFO_COMP_STRIDE (info, 0);
  image->stride[VPX_PLANE_U] = GST_VIDEO_INFO_COMP_STRIDE (info, 1);
  image->stride[VPX_PLANE_V] = GST_VIDEO_INFO_COMP_STRIDE (info, 2);

  profile_str = g_strdup_printf ("%d", encoder->cfg.g_profile);
  caps = gst_caps_new_simple ("video/x-vp9",
      "profile", G_TYPE_STRING, profile_str, NULL);
  g_free (profile_str);

  g_mutex_unlock (&encoder->encoder_lock);

  output_state =
      gst_video_encoder_set_output_state (video_encoder, caps, state);
  gst_video_codec_state_unref (output_state);

  gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));

  return ret;
}

static GstFlowReturn
gst_vp9_enc_process (GstVP9Enc * encoder)
{
  vpx_codec_iter_t iter = NULL;
  const vpx_codec_cx_pkt_t *pkt;
  GstVideoEncoder *video_encoder;
  GstVideoCodecFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;

  video_encoder = GST_VIDEO_ENCODER (encoder);

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
    frame = gst_video_encoder_get_oldest_frame (video_encoder);
    g_assert (frame != NULL);
    if ((pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0)
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    else
      GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);

    /* FIXME : It would be nice to avoid the memory copy ... */
    buffer =
        gst_buffer_new_wrapped (g_memdup (pkt->data.frame.buf,
            pkt->data.frame.sz), pkt->data.frame.sz);

    if (invisible) {
      g_mutex_unlock (&encoder->encoder_lock);
      ret = gst_pad_push (GST_VIDEO_ENCODER_SRC_PAD (encoder), buffer);
      g_mutex_lock (&encoder->encoder_lock);
      gst_video_codec_frame_unref (frame);
    } else {
      frame->output_buffer = buffer;
      g_mutex_unlock (&encoder->encoder_lock);
      ret = gst_video_encoder_finish_frame (video_encoder, frame);
      g_mutex_lock (&encoder->encoder_lock);
    }

    pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  }
  g_mutex_unlock (&encoder->encoder_lock);

  return ret;
}

static GstFlowReturn
gst_vp9_enc_finish (GstVideoEncoder * video_encoder)
{
  GstVP9Enc *encoder;
  int flags = 0;
  vpx_codec_err_t status;

  GST_DEBUG_OBJECT (video_encoder, "finish");

  encoder = GST_VP9_ENC (video_encoder);

  g_mutex_lock (&encoder->encoder_lock);
  status =
      vpx_codec_encode (&encoder->encoder, NULL, encoder->n_frames, 1, flags,
      encoder->deadline);
  g_mutex_unlock (&encoder->encoder_lock);
  if (status != 0) {
    GST_ERROR_OBJECT (encoder, "encode returned %d %s", status,
        gst_vpx_error_name (status));
    return GST_FLOW_ERROR;
  }

  /* dispatch remaining frames */
  gst_vp9_enc_process (encoder);

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

  return GST_FLOW_OK;
}

static vpx_image_t *
gst_vp9_enc_buffer_to_image (GstVP9Enc * enc, GstVideoFrame * frame)
{
  vpx_image_t *image = g_slice_new (vpx_image_t);

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
gst_vp9_enc_handle_frame (GstVideoEncoder * video_encoder,
    GstVideoCodecFrame * frame)
{
  GstVP9Enc *encoder;
  vpx_codec_err_t status;
  int flags = 0;
  vpx_image_t *image;
  GstVideoFrame vframe;

  GST_DEBUG_OBJECT (video_encoder, "handle_frame");

  encoder = GST_VP9_ENC (video_encoder);

  encoder->n_frames++;

  GST_DEBUG_OBJECT (video_encoder, "size %d %d",
      GST_VIDEO_INFO_WIDTH (&encoder->input_state->info),
      GST_VIDEO_INFO_HEIGHT (&encoder->input_state->info));

  gst_video_frame_map (&vframe, &encoder->input_state->info,
      frame->input_buffer, GST_MAP_READ);
  image = gst_vp9_enc_buffer_to_image (encoder, &vframe);

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    flags |= VPX_EFLAG_FORCE_KF;
  }

  g_mutex_lock (&encoder->encoder_lock);
  status = vpx_codec_encode (&encoder->encoder, image,
      encoder->n_frames, 1, flags, encoder->deadline);
  g_mutex_unlock (&encoder->encoder_lock);
  gst_video_frame_unmap (&vframe);

  if (status != 0) {
    GST_ELEMENT_ERROR (encoder, LIBRARY, ENCODE,
        ("Failed to encode frame"), ("%s", gst_vpx_error_name (status)));
    return FALSE;
  }
  gst_video_codec_frame_unref (frame);
  return gst_vp9_enc_process (encoder);
}

static gboolean
gst_vp9_enc_sink_event (GstVideoEncoder * benc, GstEvent * event)
{
  GstVP9Enc *enc = GST_VP9_ENC (benc);

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
gst_vp9_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

#endif /* HAVE_VP9_ENCODER */
