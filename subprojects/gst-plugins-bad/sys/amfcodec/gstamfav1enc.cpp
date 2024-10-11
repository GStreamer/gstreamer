/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
 * Copyright (C) 2022 Evgeny Pavlov <lucenticus@gmail.com>
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
 * SECTION:element-amfav1enc
 * @title: amfav1enc
 * @short_description: An AMD AMF API based AV1 video encoder
 *
 * amfav1enc element encodes raw video stream into compressed AV1 bitstream
 * via AMD AMF API.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=100 ! amfav1enc ! av1parse ! webmmux ! filesink location=encoded.webm
 * ```
 *
 * Since: 1.22
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamfav1enc.h"
#include <components/Component.h>
#include <components/VideoEncoderAV1.h>
#include <core/Factory.h>
#include <set>
#include <string>
#include <vector>
#include <string.h>

using namespace amf;

GST_DEBUG_CATEGORY_STATIC (gst_amf_av1_enc_debug);
#define GST_CAT_DEFAULT gst_amf_av1_enc_debug

static GTypeClass *parent_class = nullptr;

typedef struct
{
  amf_int64 num_of_hw_instances;
  amf_int64 max_throughput;
  amf_int64 requested_throughput;
  amf_int64 color_conversion;
  amf_int64 pre_analysis;
  amf_int64 max_bitrate;
  amf_int64 max_profile;
  amf_int64 max_level;
  amf_int64 max_num_temporal_layers;
  amf_int64 max_num_ltr_frames;
  amf_int64 default_qp_i;
  amf_int64 default_qp_p;
  amf_int64 min_gop_size;
  amf_int64 max_gop_size;
  amf_int64 default_gop_size;
  guint valign;
  gboolean pre_encode_supported;
  gboolean smart_access_supported;
  GstAmfEncoderPASupportedOptions pa_supported;
} GstAmfAv1EncDeviceCaps;

/**
 * GstAmfAv1EncUsage:
 *
 * Encoder usages
 *
 * Since: 1.22
 */
#define GST_TYPE_AMF_AV1_ENC_USAGE (gst_amf_av1_enc_usage_get_type ())
static GType
gst_amf_av1_enc_usage_get_type (void)
{
  static GType usage_type = 0;
  static const GEnumValue usages[] = {
    /**
     * GstAmfAv1EncUsage::transcoding:
     *
     * Transcoding usage
     */
    {AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING, "Transcoding", "transcoding"},

    /**
     * GstAmfAv1EncUsage::low-latency:
     *
     * Low Latency usage
     */
    {AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY, "Low Latency", "low-latency"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&usage_type)) {
    GType type = g_enum_register_static ("GstAmfAv1EncUsage", usages);
    g_once_init_leave (&usage_type, type);
  }

  return usage_type;
}

/**
 * GstAmfAv1EncRateControl:
 *
 * Rate control methods
 *
 * Since: 1.22
 */
#define GST_TYPE_AMF_AV1_ENC_RATE_CONTROL (gst_amf_av1_enc_rate_control_get_type ())
static GType
gst_amf_av1_enc_rate_control_get_type (void)
{
  static GType rate_control_type = 0;
  static const GEnumValue rate_controls[] = {
    /**
     * GstAmfAv1EncRateControl::default:
     *
     * Default rate control method depending on usage
     */
    {AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_UNKNOWN,
        "Default, depends on Usage", "default"},

    /**
     * GstAmfAv1EncRateControl::cqp:
     *
     * Constant QP
     */
    {AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP, "Constant QP",
        "cqp"},

    /**
     * GstAmfAv1EncRateControl::lcvbr:
     *
     * Latency Constrained Variable Bitrate
     */
    {AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
        "Latency Constrained VBR", "lcvbr"},

    /**
     * GstAmfAv1EncRateControl::vbr:
     *
     * Peak Constrained Variable Bitrate
     */
    {AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
        "Peak Constrained VBR", "vbr"},

    /**
     * GstAmfAv1EncRateControl::cbr:
     *
     * Constant Bitrate
     */
    {AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR, "Constant Bitrate", "cbr"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&rate_control_type)) {
    GType type =
        g_enum_register_static ("GstAmfAv1EncRateControl", rate_controls);
    g_once_init_leave (&rate_control_type, type);
  }

  return rate_control_type;
}

/**
 * GstAmfAv1EncPreset:
 *
 * Encoding quality presets
 *
 * Since: 1.22
 */
#define AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_UNKNOWN -1
#define GST_TYPE_AMF_AV1_ENC_PRESET (gst_amf_av1_enc_preset_get_type ())
static GType
gst_amf_av1_enc_preset_get_type (void)
{
  static GType preset_type = 0;
  static const GEnumValue presets[] = {
    /**
     * GstAmfAv1EncRateControl::default:
     *
     * Default preset depends on usage
     */
    {AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_UNKNOWN, "Default, depends on USAGE",
        "default"},

    /**
     * GstAmfAv1EncRateControl::high-quality:
     *
     * High quality oriented preset
     */
    {AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY, "High quality",
        "high-quality"},

    /**
     * GstAmfAv1EncRateControl::quality:
     *
     * Quality oriented preset
     */
    {AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY, "Quality", "quality"},

    /**
     *
     * GstAmfAv1EncRateControl::balanced:
     *
     * Balanced preset
     */
    {AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED, "Balanced", "balanced"},

    /**
     * GstAmfAv1EncRateControl::speed:
     *
     * Speed oriented preset
     */
    {AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED, "Speed", "speed"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&preset_type)) {
    GType type = g_enum_register_static ("GstAmfAv1EncPreset", presets);
    g_once_init_leave (&preset_type, type);
  }

  return preset_type;
}

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;

  gint64 adapter_luid;

  GstAmfAv1EncDeviceCaps dev_caps;
} GstAmfAv1EncClassData;

enum
{
  PROP_0,
  PROP_ADAPTER_LUID,
  PROP_USAGE,
  PROP_RATE_CONTROL,
  PROP_PRESET,
  PROP_BITRATE,
  PROP_MAX_BITRATE,
  PROP_GOP_SIZE,
  PROP_MIN_QP_I,
  PROP_MAX_QP_I,
  PROP_MIN_QP_P,
  PROP_MAX_QP_P,
  PROP_QP_I,
  PROP_QP_P,
  PROP_REF_FRAMES,
  PROP_SMART_ACCESS,
  PROP_PRE_ENCODE,
  PROP_PRE_ANALYSIS,
  PROP_PA_ACTIVITY_TYPE,
  PROP_PA_SCENE_CHANGE_DETECTION,
  PROP_PA_SCENE_CHANGE_DETECTION_SENSITIVITY,
  PROP_PA_STATIC_SCENE_DETECTION,
  PROP_PA_STATIC_SCENE_DETECTION_SENSITIVITY,
  PROP_PA_INITIAL_QP,
  PROP_PA_MAX_QP,
  PROP_PA_CAQ_STRENGTH,
  PROP_PA_FRAME_SAD,
  PROP_PA_LTR,
  PROP_PA_LOOKAHEAD_BUFFER_DEPTH,
  PROP_PA_PAQ_MODE,
  PROP_PA_TAQ_MODE,
  PROP_PA_HQMB_MODE,
};

#define DEFAULT_USAGE AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING
#define DEFAULT_RATE_CONTROL AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_UNKNOWN
#define DEFAULT_PRESET AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_UNKNOWN
#define DEFAULT_BITRATE 0
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_MIN_MAX_QP -1
#define DEFAULT_REF_FRAMES 1
#define DEFAULT_SMART_ACCESS FALSE
#define DEFAULT_PRE_ENCODE FALSE

#define DOC_SINK_CAPS_COMM \
    "format = (string) {NV12, P010_10LE}, " \
    "width = (int) [ 128, 4096 ], height = (int) [ 128, 4096 ]"

#define DOC_SINK_CAPS \
    "video/x-raw(memory:D3D11Memory), " DOC_SINK_CAPS_COMM "; " \
    "video/x-raw, " DOC_SINK_CAPS_COMM

#define DOC_SRC_CAPS \
    "video/x-av1, width = (int) [ 128, 4096 ], height = (int) [ 128, 4096 ], " \
    "profile = (string) main, stream-format = (string) obu-stream, alignment= (string) tu"

typedef struct _GstAmfAv1Enc
{
  GstAmfEncoder parent;

  GMutex prop_lock;
  gboolean property_updated;
  gint usage;
  gint rate_control;
  gint preset;
  guint bitrate;
  guint max_bitrate;
  guint gop_size;
  gint min_qp_i;
  gint max_qp_i;
  gint min_qp_p;
  gint max_qp_p;
  guint qp_i;
  guint qp_p;
  guint ref_frames;
  gboolean smart_access;
  gboolean pre_encode;
  GstAmfEncoderPreAnalysis pa;
} GstAmfAv1Enc;

typedef struct _GstAmfAv1EncClass
{
  GstAmfEncoderClass parent_class;
  GstAmfAv1EncDeviceCaps dev_caps;

  gint64 adapter_luid;
} GstAmfAv1EncClass;

#define GST_AMF_AV1_ENC(object) ((GstAmfAv1Enc *) (object))
#define GST_AMF_AV1_ENC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstAmfAv1EncClass))

static void gst_amf_av1_enc_finalize (GObject * object);
static void gst_amf_av1_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amf_av1_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_amf_av1_enc_set_format (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component, guint * num_reorder_frames);
static gboolean gst_amf_av1_enc_set_output_state (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component);
static gboolean gst_amf_av1_enc_set_surface_prop (GstAmfEncoder * encoder,
    GstVideoCodecFrame * frame, gpointer surface);
static GstBuffer *gst_amf_av1_enc_create_output_buffer (GstAmfEncoder *
    encoder, gpointer data, gboolean * sync_point);
static gboolean gst_amf_av1_enc_check_reconfigure (GstAmfEncoder * encoder);

static void
gst_amf_av1_enc_class_init (GstAmfAv1EncClass * klass, gpointer data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAmfEncoderClass *amf_class = GST_AMF_ENCODER_CLASS (klass);
  GstAmfAv1EncClassData *cdata = (GstAmfAv1EncClassData *) data;
  GstAmfAv1EncDeviceCaps *dev_caps = &cdata->dev_caps;
  GstAmfEncoderPASupportedOptions *pa_supported = &dev_caps->pa_supported;
  GParamFlags param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_MUTABLE_PLAYING | G_PARAM_STATIC_STRINGS);
  GParamFlags pa_param_flags = (GParamFlags) (G_PARAM_READWRITE |
      G_PARAM_STATIC_STRINGS | GST_PARAM_CONDITIONALLY_AVAILABLE);
  GstPadTemplate *pad_templ;
  GstCaps *doc_caps;

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  object_class->finalize = gst_amf_av1_enc_finalize;
  object_class->set_property = gst_amf_av1_enc_set_property;
  object_class->get_property = gst_amf_av1_enc_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of associated GPU",
          G_MININT64, G_MAXINT64, 0, (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_USAGE,
      g_param_spec_enum ("usage", "Usage",
          "Target usage", GST_TYPE_AMF_AV1_ENC_USAGE,
          DEFAULT_USAGE, param_flags));
  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Rate Control Method", GST_TYPE_AMF_AV1_ENC_RATE_CONTROL,
          DEFAULT_RATE_CONTROL, param_flags));
  g_object_class_install_property (object_class, PROP_PRESET,
      g_param_spec_enum ("preset", "Preset",
          "Preset", GST_TYPE_AMF_AV1_ENC_PRESET, DEFAULT_PRESET, param_flags));
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Target bitrate in kbit/sec (0: USAGE default)",
          0, G_MAXINT / 1000, DEFAULT_BITRATE, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Maximum bitrate in kbit/sec (0: USAGE default)",
          0, G_MAXINT / 1000, DEFAULT_MAX_BITRATE, param_flags));
  g_object_class_install_property (object_class, PROP_GOP_SIZE,
      g_param_spec_uint ("gop-size", "GOP Size",
          "Number of pictures within a GOP",
          (guint) dev_caps->min_gop_size, (guint) dev_caps->max_gop_size,
          (guint) dev_caps->default_gop_size, param_flags));
  g_object_class_install_property (object_class, PROP_MIN_QP_I,
      g_param_spec_int ("min-qp-i", "Min QP I",
          "Minimum allowed QP value for I frames (-1: USAGE default)",
          -1, 255, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_QP_I,
      g_param_spec_int ("max-qp-i", "Max QP I",
          "Maximum allowed QP value for I frames (-1: USAGE default)",
          -1, 255, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MIN_QP_P,
      g_param_spec_int ("min-qp-p", "Min QP P",
          "Minimum allowed QP value for P frames (-1: USAGE default)",
          -1, 255, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_QP_P,
      g_param_spec_int ("max-qp-p", "Max QP P",
          "Maximum allowed QP value for P frames (-1: USAGE default)",
          -1, 255, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_I,
      g_param_spec_uint ("qp-i", "QP I",
          "Constant QP for I frames", 0, 255,
          (guint) dev_caps->default_qp_i, param_flags));
  g_object_class_install_property (object_class, PROP_QP_P,
      g_param_spec_uint ("qp-p", "QP P",
          "Constant QP for P frames", 0, 255,
          (guint) dev_caps->default_qp_p, param_flags));
  g_object_class_install_property (object_class, PROP_REF_FRAMES,
      g_param_spec_uint ("ref-frames", "Reference Frames",
          "Number of reference frames", 0, 8, DEFAULT_REF_FRAMES, param_flags));

  if (cdata->dev_caps.pre_encode_supported) {
    /**
     * GstAmfAv1Enc:pre-encode:
     *
     * Enable pre-encode assisted rate control
     *
     * Since: 1.24
     */
    g_object_class_install_property (object_class, PROP_PRE_ENCODE,
        g_param_spec_boolean ("pre-encode", "Pre-encode",
            "Enable pre-encode", DEFAULT_PRE_ENCODE,
            (GParamFlags) (param_flags | GST_PARAM_CONDITIONALLY_AVAILABLE)));
  }

  if (cdata->dev_caps.smart_access_supported) {
    /**
     * GstAmfAv1Enc:smart-access-video:
     *
     * Enable AMF SmartAccess Video feature
     *
     * Since: 1.24
     */
    g_object_class_install_property (object_class, PROP_SMART_ACCESS,
        g_param_spec_boolean ("smart-access-video", "Smart Access Video",
            "Enable AMF SmartAccess Video feature for optimal distribution"
            " between multiple AMD hardware instances", DEFAULT_SMART_ACCESS,
            (GParamFlags) (G_PARAM_READWRITE |
                GST_PARAM_CONDITIONALLY_AVAILABLE |
                GST_PARAM_MUTABLE_PLAYING | G_PARAM_STATIC_STRINGS)));
  }

  if (dev_caps->pre_analysis) {
    /**
     * GstAmfAv1Enc:pre-analysis:
     *
     * Enable the pre-analysis module
     *
     * Since: 1.24
     */
    g_object_class_install_property (object_class, PROP_PRE_ANALYSIS,
        g_param_spec_boolean ("pre-analysis", "Pre-analysis",
            "Enable pre-analysis", DEFAULT_PRE_ANALYSIS, param_flags));

    if (pa_supported->activity_type) {
      /**
       * GstAmfAv1Enc:pa-activity-type:
       *
       * Set the type of activity analysis for pre-analysis
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_ACTIVITY_TYPE,
          g_param_spec_enum ("pa-activity-type", "Pre-analysis activity type",
              "Set the type of activity analysis for pre-analysis",
              GST_TYPE_AMF_ENC_PA_ACTIVITY_TYPE, DEFAULT_PA_ACTIVITY_TYPE,
              pa_param_flags));
    }

    if (pa_supported->scene_change_detection) {
      /**
       * GstAmfAv1Enc:pa-scene-change-detection:
       *
       * Enable scene change detection for pre-analysis
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class,
          PROP_PA_SCENE_CHANGE_DETECTION,
          g_param_spec_boolean ("pa-scene-change-detection",
              "Pre-analysis scene change detection",
              "Enable scene change detection for pre-analysis",
              DEFAULT_PA_SCENE_CHANGE_DETECTION, pa_param_flags));
    }

    if (pa_supported->scene_change_detection_sensitivity) {
      /**
       * GstAmfAv1Enc:pa-scene-change-detection-sensitivity:
       *
       * Pre-analysis scene change detection sensitivity
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class,
          PROP_PA_SCENE_CHANGE_DETECTION_SENSITIVITY,
          g_param_spec_enum ("pa-scene-change-detection-sensitivity",
              "Pre-analysis scene change detection sensitivity",
              "Set the sensitivity of scene change detection for pre-analysis",
              GST_TYPE_AMF_ENC_PA_SCENE_CHANGE_DETECTION_SENSITIVITY,
              DEFAULT_PA_SCENE_CHANGE_DETECTION_SENSITIVITY, pa_param_flags));
    }

    if (pa_supported->static_scene_detection) {
      /**
       * GstAmfAv1Enc:pa-static-scene-detection:
       *
       * Enable static scene detection for pre-analysis
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class,
          PROP_PA_STATIC_SCENE_DETECTION,
          g_param_spec_boolean ("pa-static-scene-detection",
              "Pre-analysis static scene detection",
              "Enable static scene detection for pre-analysis",
              DEFAULT_PA_STATIC_SCENE_DETECTION, pa_param_flags));
    }

    if (pa_supported->static_scene_detection_sensitivity) {
      /**
       * GstAmfAv1Enc:pa-static-scene-detection-sensitivity:
       *
       * Pre-analysis static scene detection sensitivity
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class,
          PROP_PA_STATIC_SCENE_DETECTION_SENSITIVITY,
          g_param_spec_enum ("pa-static-scene-detection-sensitivity",
              "Pre-analysis static scene detection sensitivity",
              "Set the sensitivity of static scene detection for pre-analysis",
              GST_TYPE_AMF_ENC_PA_STATIC_SCENE_DETECTION_SENSITIVITY,
              DEFAULT_PA_STATIC_SCENE_DETECTION_SENSITIVITY, pa_param_flags));
    }

    if (pa_supported->initial_qp) {
      /**
       * GstAmfAv1Enc:pa-initial-qp:
       *
       * Pre-analysis initial QP
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_INITIAL_QP,
          g_param_spec_uint ("pa-initial-qp", "Pre-analysis initial QP",
              "The QP value that is used immediately after a scene change", 0,
              51, DEFAULT_PA_INITIAL_QP, pa_param_flags));
    }

    if (pa_supported->max_qp) {
      /**
       * GstAmfAv1Enc:pa-max-qp:
       *
       * Pre-analysis max QP
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_MAX_QP,
          g_param_spec_uint ("pa-max-qp", "Pre-analysis max QP",
              "The QP threshold to allow a skip frame", 0, 51,
              DEFAULT_PA_MAX_QP, pa_param_flags));
    }

    if (pa_supported->caq_strength) {
      /**
       * GstAmfAv1Enc:pa-caq-strength:
       *
       * Content Adaptive Quantization strength for pre-analysis
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_CAQ_STRENGTH,
          g_param_spec_enum ("pa-caq-strength", "Pre-analysis CAQ strength",
              "Content Adaptive Quantization strength for pre-analysis",
              GST_TYPE_AMF_ENC_PA_CAQ_STRENGTH, DEFAULT_PA_CAQ_STRENGTH,
              pa_param_flags));
    }

    if (pa_supported->frame_sad) {
      /**
       * GstAmfAv1Enc:pa-frame-sad:
       *
       * Enable Frame SAD algorithm
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_FRAME_SAD,
          g_param_spec_boolean ("pa-frame-sad", "Pre-analysis SAD algorithm",
              "Enable Frame SAD algorithm", DEFAULT_PA_FRAME_SAD,
              pa_param_flags));
    }

    if (pa_supported->ltr) {
      /**
       * GstAmfAv1Enc:pa-ltr:
       *
       * Enable long term reference frame management
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_LTR,
          g_param_spec_boolean ("pa-ltr", "Pre-analysis LTR",
              "Enable long term reference frame management", DEFAULT_PA_LTR,
              pa_param_flags));
    }

    if (pa_supported->lookahead_buffer_depth) {
      /**
       * GstAmfAv1Enc:pa-lookahead-buffer-depth:
       *
       * Pre-analysis lookahead buffer depth
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class,
          PROP_PA_LOOKAHEAD_BUFFER_DEPTH,
          g_param_spec_uint ("pa-lookahead-buffer-depth",
              "Pre-analysis lookahead buffer depth",
              "Set the PA lookahead buffer size", 0, 41,
              DEFAULT_PA_LOOKAHEAD_BUFFER_DEPTH, pa_param_flags));
    }

    if (pa_supported->paq_mode) {
      /**
       * GstAmfAv1Enc:pa-paq-mode:
       *
       * Set the perceptual adaptive quantization mode
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_PAQ_MODE,
          g_param_spec_enum ("pa-paq-mode", "Pre-analysis PAQ mode",
              "Set the perceptual adaptive quantization mode",
              GST_TYPE_AMF_ENC_PA_PAQ_MODE, DEFAULT_PA_PAQ_MODE,
              pa_param_flags));
    }

    if (pa_supported->taq_mode) {
      /**
       * GstAmfAv1Enc:pa-taq-mode:
       *
       * Set the temporal adaptive quantization mode
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_TAQ_MODE,
          g_param_spec_enum ("pa-taq-mode", "Pre-analysis TAQ mode",
              "Set the temporal adaptive quantization mode",
              GST_TYPE_AMF_ENC_PA_TAQ_MODE, DEFAULT_PA_TAQ_MODE,
              pa_param_flags));
    }

    if (pa_supported->hmqb_mode) {
      /**
       * GstAmfAv1Enc:pa-hqmb-mode:
       *
       * Set the PA high motion quality boost mode
       *
       * Since: 1.24
       */
      g_object_class_install_property (object_class, PROP_PA_HQMB_MODE,
          g_param_spec_enum ("pa-hqmb-mode", "Pre-analysis HQMB mode",
              "Set the PA high motion quality boost mode",
              GST_TYPE_AMF_ENC_PA_HQMB_MODE, DEFAULT_PA_HQMB_MODE,
              pa_param_flags));
    }
  }

  gst_element_class_set_metadata (element_class,
      "AMD AMF AV1 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode AV1 video streams using AMF API",
      "Seungha Yang <seungha@centricular.com>, "
      "Evgeny Pavlov <lucenticus@gmail.com>");

  pad_templ = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, cdata->sink_caps);
  doc_caps = gst_caps_from_string (DOC_SINK_CAPS);
  gst_pad_template_set_documentation_caps (pad_templ, doc_caps);
  gst_caps_unref (doc_caps);
  gst_element_class_add_pad_template (element_class, pad_templ);

  pad_templ = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, cdata->src_caps);
  doc_caps = gst_caps_from_string (DOC_SRC_CAPS);
  gst_pad_template_set_documentation_caps (pad_templ, doc_caps);
  gst_caps_unref (doc_caps);
  gst_element_class_add_pad_template (element_class, pad_templ);

  amf_class->set_format = GST_DEBUG_FUNCPTR (gst_amf_av1_enc_set_format);
  amf_class->set_output_state =
      GST_DEBUG_FUNCPTR (gst_amf_av1_enc_set_output_state);
  amf_class->set_surface_prop =
      GST_DEBUG_FUNCPTR (gst_amf_av1_enc_set_surface_prop);
  amf_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_amf_av1_enc_create_output_buffer);
  amf_class->check_reconfigure =
      GST_DEBUG_FUNCPTR (gst_amf_av1_enc_check_reconfigure);

  klass->dev_caps = cdata->dev_caps;
  klass->adapter_luid = cdata->adapter_luid;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);

  gst_type_mark_as_plugin_api (GST_TYPE_AMF_AV1_ENC_USAGE,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_AMF_AV1_ENC_RATE_CONTROL,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_AMF_AV1_ENC_PRESET,
      (GstPluginAPIFlags) 0);
}

static void
gst_amf_av1_enc_init (GstAmfAv1Enc * self)
{
  GstAmfAv1EncClass *klass = GST_AMF_AV1_ENC_GET_CLASS (self);
  GstAmfAv1EncDeviceCaps *dev_caps = &klass->dev_caps;

  gst_amf_encoder_set_subclass_data (GST_AMF_ENCODER (self),
      klass->adapter_luid, AMFVideoEncoder_AV1);

  g_mutex_init (&self->prop_lock);

  self->usage = DEFAULT_USAGE;
  self->rate_control = DEFAULT_RATE_CONTROL;
  self->preset = DEFAULT_PRESET;
  self->bitrate = DEFAULT_BITRATE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->gop_size = (guint) dev_caps->default_gop_size;
  self->min_qp_i = DEFAULT_MIN_MAX_QP;
  self->max_qp_i = DEFAULT_MIN_MAX_QP;
  self->min_qp_p = DEFAULT_MIN_MAX_QP;
  self->max_qp_p = DEFAULT_MIN_MAX_QP;
  self->qp_i = (guint) dev_caps->default_qp_i;
  self->qp_p = (guint) dev_caps->default_qp_p;
  self->ref_frames = DEFAULT_REF_FRAMES;
  self->smart_access = DEFAULT_SMART_ACCESS;
  self->pre_encode = DEFAULT_PRE_ENCODE;
  // Init pre-analysis options
  self->pa.pre_analysis = DEFAULT_PRE_ANALYSIS;
  self->pa.activity_type = DEFAULT_PA_ACTIVITY_TYPE;
  self->pa.scene_change_detection = DEFAULT_PA_SCENE_CHANGE_DETECTION;
  self->pa.scene_change_detection_sensitivity =
      DEFAULT_PA_SCENE_CHANGE_DETECTION_SENSITIVITY;
  self->pa.static_scene_detection = DEFAULT_PA_STATIC_SCENE_DETECTION;
  self->pa.static_scene_detection_sensitivity =
      DEFAULT_PA_STATIC_SCENE_DETECTION_SENSITIVITY;
  self->pa.initial_qp = DEFAULT_PA_INITIAL_QP;
  self->pa.max_qp = DEFAULT_PA_MAX_QP;
  self->pa.caq_strength = DEFAULT_PA_CAQ_STRENGTH;
  self->pa.frame_sad = DEFAULT_PA_FRAME_SAD;
  self->pa.ltr = DEFAULT_PA_LTR;
  self->pa.lookahead_buffer_depth = DEFAULT_PA_LOOKAHEAD_BUFFER_DEPTH;
  self->pa.paq_mode = DEFAULT_PA_PAQ_MODE;
  self->pa.taq_mode = DEFAULT_PA_TAQ_MODE;
  self->pa.hmqb_mode = DEFAULT_PA_HQMB_MODE;
}

static void
gst_amf_av1_enc_finalize (GObject * object)
{
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (object);

  g_mutex_clear (&self->prop_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_int (GstAmfAv1Enc * self, gint * old_val, const GValue * new_val)
{
  gint val = g_value_get_int (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_uint (GstAmfAv1Enc * self, guint * old_val, const GValue * new_val)
{
  guint val = g_value_get_uint (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_enum (GstAmfAv1Enc * self, gint * old_val, const GValue * new_val)
{
  gint val = g_value_get_enum (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_bool (GstAmfAv1Enc * self, gboolean * old_val, const GValue * new_val)
{
  gboolean val = g_value_get_boolean (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
gst_amf_av1_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (object);

  g_mutex_lock (&self->prop_lock);
  switch (prop_id) {
    case PROP_USAGE:
      update_enum (self, &self->usage, value);
      break;
    case PROP_RATE_CONTROL:
      update_enum (self, &self->rate_control, value);
      break;
    case PROP_PRESET:
      update_enum (self, &self->preset, value);
      break;
    case PROP_BITRATE:
      update_uint (self, &self->bitrate, value);
      break;
    case PROP_MAX_BITRATE:
      update_uint (self, &self->max_bitrate, value);
      break;
    case PROP_GOP_SIZE:
      update_uint (self, &self->gop_size, value);
      break;
    case PROP_MIN_QP_I:
      update_int (self, &self->min_qp_i, value);
      break;
    case PROP_MAX_QP_I:
      update_int (self, &self->max_qp_i, value);
      break;
    case PROP_MIN_QP_P:
      update_int (self, &self->min_qp_p, value);
      break;
    case PROP_MAX_QP_P:
      update_int (self, &self->max_qp_p, value);
      break;
    case PROP_QP_I:
      update_uint (self, &self->qp_i, value);
      break;
    case PROP_QP_P:
      update_uint (self, &self->qp_p, value);
      break;
    case PROP_REF_FRAMES:
      update_uint (self, &self->ref_frames, value);
      break;
    case PROP_SMART_ACCESS:
      update_bool (self, &self->smart_access, value);
      break;
    case PROP_PRE_ENCODE:
      update_bool (self, &self->pre_encode, value);
      break;
    case PROP_PRE_ANALYSIS:
      update_bool (self, &self->pa.pre_analysis, value);
      break;
    case PROP_PA_ACTIVITY_TYPE:
      update_enum (self, &self->pa.activity_type, value);
      break;
    case PROP_PA_SCENE_CHANGE_DETECTION:
      update_bool (self, &self->pa.scene_change_detection, value);
      break;
    case PROP_PA_SCENE_CHANGE_DETECTION_SENSITIVITY:
      update_enum (self, &self->pa.scene_change_detection_sensitivity, value);
      break;
    case PROP_PA_STATIC_SCENE_DETECTION:
      update_bool (self, &self->pa.static_scene_detection, value);
      break;
    case PROP_PA_STATIC_SCENE_DETECTION_SENSITIVITY:
      update_enum (self, &self->pa.static_scene_detection_sensitivity, value);
      break;
    case PROP_PA_INITIAL_QP:
      update_uint (self, &self->pa.initial_qp, value);
      break;
    case PROP_PA_MAX_QP:
      update_uint (self, &self->pa.max_qp, value);
      break;
    case PROP_PA_CAQ_STRENGTH:
      update_enum (self, &self->pa.caq_strength, value);
      break;
    case PROP_PA_FRAME_SAD:
      update_bool (self, &self->pa.frame_sad, value);
    case PROP_PA_LTR:
      update_bool (self, &self->pa.ltr, value);
      break;
    case PROP_PA_LOOKAHEAD_BUFFER_DEPTH:
      update_uint (self, &self->pa.lookahead_buffer_depth, value);
      break;
    case PROP_PA_PAQ_MODE:
      update_enum (self, &self->pa.paq_mode, value);
      break;
    case PROP_PA_TAQ_MODE:
      update_enum (self, &self->pa.taq_mode, value);
      break;
    case PROP_PA_HQMB_MODE:
      update_enum (self, &self->pa.hmqb_mode, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&self->prop_lock);
}

static void
gst_amf_av1_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmfAv1EncClass *klass = GST_AMF_AV1_ENC_GET_CLASS (object);
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (object);

  switch (prop_id) {
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, klass->adapter_luid);
      break;
    case PROP_USAGE:
      g_value_set_enum (value, self->usage);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->rate_control);
      break;
    case PROP_PRESET:
      g_value_set_enum (value, self->preset);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, self->max_bitrate);
      break;
    case PROP_GOP_SIZE:
      g_value_set_uint (value, self->gop_size);
      break;
    case PROP_MIN_QP_I:
      g_value_set_int (value, self->min_qp_i);
      break;
    case PROP_MAX_QP_I:
      g_value_set_int (value, self->max_qp_i);
      break;
    case PROP_MIN_QP_P:
      g_value_set_int (value, self->min_qp_p);
      break;
    case PROP_MAX_QP_P:
      g_value_set_int (value, self->max_qp_p);
      break;
    case PROP_QP_I:
      g_value_set_uint (value, self->qp_i);
      break;
    case PROP_QP_P:
      g_value_set_uint (value, self->qp_p);
      break;
    case PROP_REF_FRAMES:
      g_value_set_uint (value, self->ref_frames);
      break;
    case PROP_SMART_ACCESS:
      g_value_set_boolean (value, self->smart_access);
      break;
    case PROP_PRE_ENCODE:
      g_value_set_boolean (value, self->pre_encode);
      break;
    case PROP_PRE_ANALYSIS:
      g_value_set_boolean (value, self->pa.pre_analysis);
      break;
    case PROP_PA_ACTIVITY_TYPE:
      g_value_set_enum (value, self->pa.activity_type);
      break;
    case PROP_PA_SCENE_CHANGE_DETECTION:
      g_value_set_boolean (value, self->pa.scene_change_detection);
      break;
    case PROP_PA_SCENE_CHANGE_DETECTION_SENSITIVITY:
      g_value_set_enum (value, self->pa.scene_change_detection_sensitivity);
      break;
    case PROP_PA_STATIC_SCENE_DETECTION:
      g_value_set_boolean (value, self->pa.static_scene_detection);
      break;
    case PROP_PA_STATIC_SCENE_DETECTION_SENSITIVITY:
      g_value_set_enum (value, self->pa.static_scene_detection_sensitivity);
      break;
    case PROP_PA_INITIAL_QP:
      g_value_set_uint (value, self->pa.initial_qp);
      break;
    case PROP_PA_MAX_QP:
      g_value_set_uint (value, self->pa.max_qp);
      break;
    case PROP_PA_CAQ_STRENGTH:
      g_value_set_enum (value, self->pa.caq_strength);
      break;
    case PROP_PA_FRAME_SAD:
      g_value_set_boolean (value, self->pa.frame_sad);
      break;
    case PROP_PA_LTR:
      g_value_set_boolean (value, self->pa.ltr);
      break;
    case PROP_PA_LOOKAHEAD_BUFFER_DEPTH:
      g_value_set_uint (value, self->pa.lookahead_buffer_depth);
      break;
    case PROP_PA_PAQ_MODE:
      g_value_set_enum (value, self->pa.paq_mode);
      break;
    case PROP_PA_TAQ_MODE:
      g_value_set_enum (value, self->pa.taq_mode);
      break;
    case PROP_PA_HQMB_MODE:
      g_value_set_enum (value, self->pa.hmqb_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_amf_av1_enc_set_format (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component, guint * num_reorder_frames)
{
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (encoder);
  GstAmfAv1EncClass *klass = GST_AMF_AV1_ENC_GET_CLASS (self);
  GstAmfAv1EncDeviceCaps *dev_caps = &klass->dev_caps;
  AMFComponent *comp = (AMFComponent *) component;
  GstVideoInfo *info = &state->info;
  AMF_RESULT result;
  AMFRate framerate;
  amf_int64 int64_val = 0;
  AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM rc_mode;
  amf_int64 color_depth;
  AMF_SURFACE_FORMAT surface_format;
  const GstVideoColorimetry *cinfo = &info->colorimetry;
  amf_int64 color_profile;

  g_mutex_lock (&self->prop_lock);
  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_FRAMESIZE,
      AMFConstructSize (info->width, info->height));
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set frame size, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_USAGE,
      (amf_int64) self->usage);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set usage, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (self->preset > AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_UNKNOWN) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET,
        (amf_int64) self->preset);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set quality preset, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_PROFILE,
      (amf_int64) AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set profile, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12) {
    color_depth = AMF_COLOR_BIT_DEPTH_8;
    surface_format = AMF_SURFACE_NV12;
  } else if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_P010_10LE) {
    color_depth = AMF_COLOR_BIT_DEPTH_10;
    surface_format = AMF_SURFACE_P010;
  } else {
    GST_ERROR_OBJECT (self, "Unexpected format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
    g_assert_not_reached ();
    goto error;
  }

  result =
      comp->SetProperty (AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, color_depth);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set bit depth, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE,
      (amf_int64) AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set alignment mode, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_MAX_NUM_REFRAMES,
      (amf_int64) self->ref_frames);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set ref-frames, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (dev_caps->smart_access_supported) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_ENABLE_SMART_ACCESS_VIDEO,
        (amf_bool) self->smart_access);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (self, "Failed to set smart access video, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    }
  }
  color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;
  switch (cinfo->matrix) {
      /* XXX: There's a driver bug that BT601 color matrix here results in
       * identity matrix specified */
#if 0
    case GST_VIDEO_COLOR_MATRIX_BT601:
      if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255) {
        color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601;
      } else {
        color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
      }
      break;
#endif
    case GST_VIDEO_COLOR_MATRIX_BT709:
      if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255) {
        color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709;
      } else {
        color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
      }
      break;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255) {
        color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020;
      } else {
        color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
      }
      break;
    default:
      break;
  }

  result =
      comp->SetProperty (AMF_VIDEO_ENCODER_AV1_OUTPUT_COLOR_PROFILE,
      color_profile);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set output color profile, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result =
      comp->SetProperty (AMF_VIDEO_ENCODER_AV1_OUTPUT_TRANSFER_CHARACTERISTIC,
      gst_video_transfer_function_to_iso (cinfo->transfer));
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self,
        "Failed to set output transfer characteristic, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_OUTPUT_COLOR_PRIMARIES,
      gst_video_color_primaries_to_iso (cinfo->primaries));
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set output color primaries, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (cinfo->transfer == GST_VIDEO_TRANSFER_SMPTE2084 &&
      state->mastering_display_info && state->content_light_level) {
    AMFBuffer *hdrmeta_buffer = NULL;
    result =
        comp->GetContext ()->AllocBuffer (AMF_MEMORY_HOST,
        sizeof (AMFHDRMetadata), &hdrmeta_buffer);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to allocate HDR metadata buffer, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
    AMFHDRMetadata *hdrmeta = (AMFHDRMetadata *) hdrmeta_buffer->GetNative ();
    GstVideoMasteringDisplayInfo *minfo = state->mastering_display_info;
    GstVideoContentLightLevel *cllinfo = state->content_light_level;
    hdrmeta->maxMasteringLuminance =
        (amf_uint32) minfo->max_display_mastering_luminance;
    hdrmeta->minMasteringLuminance =
        (amf_uint32) minfo->min_display_mastering_luminance;

    hdrmeta->redPrimary[0] = minfo->display_primaries[0].x;
    hdrmeta->redPrimary[1] = minfo->display_primaries[0].y;

    hdrmeta->greenPrimary[0] = minfo->display_primaries[1].x;
    hdrmeta->greenPrimary[1] = minfo->display_primaries[1].y;

    hdrmeta->bluePrimary[0] = minfo->display_primaries[2].x;
    hdrmeta->bluePrimary[1] = minfo->display_primaries[2].y;

    hdrmeta->whitePoint[0] = minfo->white_point.x;
    hdrmeta->whitePoint[1] = minfo->white_point.y;

    hdrmeta->maxContentLightLevel =
        (amf_uint16) cllinfo->max_content_light_level;
    hdrmeta->maxFrameAverageLightLevel =
        (amf_uint16) cllinfo->max_frame_average_light_level;

    result =
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_INPUT_HDR_METADATA,
        hdrmeta_buffer);

    hdrmeta_buffer->Release ();
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set HDR metadata, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  if (dev_caps->pre_encode_supported) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_PREENCODE,
        (amf_bool) self->pre_encode);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set pre-encode, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  if (dev_caps->pre_analysis) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE,
        (amf_bool) self->pa.pre_analysis);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set pre-analysis, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
    if (self->pa.pre_analysis) {
      result =
          gst_amf_encoder_set_pre_analysis_options (encoder, comp, &self->pa,
          &dev_caps->pa_supported);
      if (result != AMF_OK)
        goto error;
    }
  }

  result = comp->Init (surface_format, info->width, info->height);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to init component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (self->rate_control != AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_UNKNOWN) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD,
        (amf_int64) self->rate_control);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set rate-control, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  result = comp->GetProperty (AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD,
      &int64_val);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to get rate-control method, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  rc_mode = (AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM) int64_val;
  if (self->min_qp_i >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTRA,
        (amf_int64) self->min_qp_i);
  }
  if (self->max_qp_i >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTRA,
        (amf_int64) self->max_qp_i);
  }
  if (self->min_qp_p >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTER,
        (amf_int64) self->min_qp_p);
  }
  if (self->max_qp_p >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTER,
        (amf_int64) self->max_qp_p);
  }

  comp->SetProperty (AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTRA,
      (amf_int64) self->qp_i);
  comp->SetProperty (AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER,
      (amf_int64) self->qp_p);

  switch (rc_mode) {
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR:
      if (self->bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE,
            (amf_int64) self->bitrate * 1000);
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE,
            (amf_int64) self->bitrate * 1000);
      }
      break;
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR:
    case AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR:
      if (self->bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE,
            (amf_int64) self->bitrate * 1000);
      }
      if (self->max_bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE,
            (amf_int64) self->max_bitrate * 1000);
      }
      break;
    default:
      break;
  }

  /* Disable frame skip for now, need investigation the behavior */
  result =
      comp->SetProperty (AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_SKIP_FRAME,
      (amf_bool) false);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to disable skip frame, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (info->fps_n > 0 && info->fps_d) {
    framerate = AMFConstructRate (info->fps_n, info->fps_d);
  } else {
    framerate = AMFConstructRate (25, 1);
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_FRAMERATE, framerate);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set frame rate, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_AV1_GOP_SIZE,
      (amf_int64) self->gop_size);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set gop-size, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  self->property_updated = FALSE;
  g_mutex_unlock (&self->prop_lock);

  return TRUE;

error:
  g_mutex_unlock (&self->prop_lock);

  return FALSE;
}

static gboolean
gst_amf_av1_enc_set_output_state (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component)
{
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (encoder);
  GstVideoCodecState *output_state;
  GstCaps *caps;
  GstTagList *tags;

  caps = gst_caps_from_string ("video/x-av1, profile = (string) main, "
      "stream-format = (string) obu-stream, alignment = (string) tu");
  output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      caps, state);

  GST_INFO_OBJECT (self, "Output caps: %" GST_PTR_FORMAT, output_state->caps);
  gst_video_codec_state_unref (output_state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      "amfav1enc", nullptr);

  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder),
      tags, GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static gboolean
gst_amf_av1_enc_set_surface_prop (GstAmfEncoder * encoder,
    GstVideoCodecFrame * frame, gpointer surface)
{
  AMFSurface *surf = (AMFSurface *) surface;
  AMF_RESULT result;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    amf_int64 type = (amf_int64) AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_NONE;
    result = surf->SetProperty (AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE, type);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (encoder, "Failed to set force frame type, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    }
  }
  return TRUE;
}

static GstBuffer *
gst_amf_av1_enc_create_output_buffer (GstAmfEncoder * encoder,
    gpointer data, gboolean * sync_point)
{
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (encoder);
  AMFBuffer *amf_buf = (AMFBuffer *) data;
  GstBuffer *buf;
  guint8 *data_ptr;
  gsize data_size;
  amf_int64 output_type = 0;
  AMF_RESULT result;

  data_ptr = (guint8 *) amf_buf->GetNative ();
  data_size = amf_buf->GetSize ();

  if (!data_ptr || data_size == 0) {
    GST_WARNING_OBJECT (self, "Empty buffer");
    return nullptr;
  }

  buf = gst_buffer_new_memdup (data_ptr, data_size);

  result = amf_buf->GetProperty (AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE,
      &output_type);
  if (result == AMF_OK &&
      output_type == (amf_int64) AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_KEY) {
    *sync_point = TRUE;
  }

  return buf;
}

static gboolean
gst_amf_av1_enc_check_reconfigure (GstAmfEncoder * encoder)
{
  GstAmfAv1Enc *self = GST_AMF_AV1_ENC (encoder);
  gboolean ret;

  g_mutex_lock (&self->prop_lock);
  ret = self->property_updated;
  g_mutex_unlock (&self->prop_lock);

  return ret;
}

static GstAmfAv1EncClassData *
gst_amf_av1_enc_create_class_data (GstD3D11Device * device, AMFComponent * comp)
{
  AMF_RESULT result;
  GstAmfAv1EncDeviceCaps dev_caps = { 0, };
  std::string sink_caps_str;
  std::string src_caps_str;
  std::vector < std::string > profiles;
  std::string resolution_str;
  GstAmfAv1EncClassData *cdata;
  AMFCapsPtr amf_caps;
  AMFIOCapsPtr in_iocaps;
  AMFIOCapsPtr out_iocaps;
  amf_int32 in_min_width = 0, in_max_width = 0;
  amf_int32 in_min_height = 0, in_max_height = 0;
  amf_int32 out_min_width = 0, out_max_width = 0;
  amf_int32 out_min_height = 0, out_max_height = 0;
  amf_bool pre_encode_supported;
  amf_bool smart_access_supported;
  amf_int32 num_val;
  std::set < std::string > formats;
  std::string format_str;
  gboolean have_nv12 = FALSE;
  gboolean d3d11_supported = FALSE;
  gint min_width, max_width, min_height, max_height;
  GstCaps *sink_caps;
  GstCaps *system_caps;

  result = comp->GetCaps (&amf_caps);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Unable to get caps");
    return nullptr;
  }

  result = amf_caps->GetInputCaps (&in_iocaps);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Unable to get input io caps");
    return nullptr;
  }

  in_iocaps->GetWidthRange (&in_min_width, &in_max_width);
  in_iocaps->GetHeightRange (&in_min_height, &in_max_height);
  dev_caps.valign = in_iocaps->GetVertAlign ();

  GST_INFO_OBJECT (device, "Input width: [%d, %d], height: [%d, %d], "
      "valign: %d", in_min_width, in_max_width, in_min_height, in_max_height,
      dev_caps.valign);

  num_val = in_iocaps->GetNumOfFormats ();
  GST_LOG_OBJECT (device, "Input format count: %d", num_val);
  for (amf_int32 i = 0; i < num_val; i++) {
    AMF_SURFACE_FORMAT format;
    amf_bool native = false;

    result = in_iocaps->GetFormatAt (i, &format, &native);
    if (result != AMF_OK)
      continue;

    GST_INFO_OBJECT (device, "Format %d supported, native %d", format, native);
    if (format == AMF_SURFACE_NV12) {
      have_nv12 = TRUE;
      formats.insert ("NV12");
    }
    if (format == AMF_SURFACE_P010 && native) {
      formats.insert ("P010_10LE");
    }
  }

  if (formats.empty ()) {
    GST_WARNING_OBJECT (device, "Empty supported input formats");
    return nullptr;
  }

  if (!have_nv12) {
    GST_WARNING_OBJECT (device, "NV12 is not supported");
    return nullptr;
  }
#define APPEND_STRING(dst,set,str) G_STMT_START { \
  if (set.find(str) != set.end()) { \
    if (!first) \
      dst += ", "; \
    dst += str; \
    first = FALSE; \
  } \
} G_STMT_END

  if (formats.size () == 1) {
    format_str = "format = (string) " + *(formats.begin ());
  } else {
    gboolean first = TRUE;
    format_str = "format = (string) { ";
    APPEND_STRING (format_str, formats, "NV12");
    APPEND_STRING (format_str, formats, "P010_10LE");
    format_str += " } ";
  }
#undef APPEND_STRING

  num_val = in_iocaps->GetNumOfMemoryTypes ();
  GST_LOG_OBJECT (device, "Input memory type count: %d", num_val);
  for (amf_int32 i = 0; i < num_val; i++) {
    AMF_MEMORY_TYPE type;
    amf_bool native = false;

    result = in_iocaps->GetMemoryTypeAt (i, &type, &native);
    if (result != AMF_OK)
      continue;

    GST_INFO_OBJECT (device,
        "MemoryType %d supported, native %d", type, native);
    if (type == AMF_MEMORY_DX11)
      d3d11_supported = TRUE;
  }

  if (!d3d11_supported) {
    GST_WARNING_OBJECT (device, "D3D11 is not supported");
    return nullptr;
  }

  result = amf_caps->GetOutputCaps (&out_iocaps);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Unable to get input io caps");
    return nullptr;
  }

  out_iocaps->GetWidthRange (&out_min_width, &out_max_width);
  out_iocaps->GetHeightRange (&out_min_height, &out_max_height);

  GST_INFO_OBJECT (device, "Output width: [%d, %d], height: [%d, %d]",
      in_min_width, in_max_width, in_min_height, in_max_height);

#define QUERY_CAPS_PROP(prop,val) G_STMT_START { \
  amf_int64 _val = 0; \
  result = amf_caps->GetProperty (prop, &_val); \
  if (result == AMF_OK) { \
    GST_INFO_OBJECT (device, G_STRINGIFY (val) ": %" G_GINT64_FORMAT, _val); \
    dev_caps.val = _val; \
  } \
} G_STMT_END
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_NUM_OF_HW_INSTANCES,
      num_of_hw_instances);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_MAX_THROUGHPUT, max_throughput);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_REQUESTED_THROUGHPUT,
      requested_throughput);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_COLOR_CONVERSION,
      color_conversion);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_PRE_ANALYSIS, pre_analysis);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_MAX_BITRATE, max_bitrate);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_MAX_PROFILE, max_profile);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_MAX_LEVEL, max_level);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_MAX_NUM_TEMPORAL_LAYERS,
      max_num_temporal_layers);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_AV1_CAP_MAX_NUM_LTR_FRAMES,
      max_num_ltr_frames);

#undef QUERY_CAPS_PROP

#define QUERY_DEFAULT_PROP(prop,val,default_val) G_STMT_START { \
  const AMFPropertyInfo *pinfo = nullptr; \
  result = comp->GetPropertyInfo (prop, &pinfo); \
  if (result == AMF_OK && pinfo) { \
    dev_caps.val = AMFVariantGetInt64 (&pinfo->defaultValue); \
    GST_INFO_OBJECT (device, G_STRINGIFY (val) ": %" G_GINT64_FORMAT, \
        dev_caps.val); \
  } else { \
    dev_caps.val = default_val; \
  } \
} G_STMT_END

  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTRA, default_qp_i, 26);
  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER, default_qp_p, 26);
#undef QUERY_DEFAULT_PROP

  result = comp->GetProperty (AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_PREENCODE,
      &pre_encode_supported);
  if (result == AMF_OK)
    dev_caps.pre_encode_supported = TRUE;

  result = comp->GetProperty (AMF_VIDEO_ENCODER_AV1_ENABLE_SMART_ACCESS_VIDEO,
      &smart_access_supported);
  if (result == AMF_OK)
    dev_caps.smart_access_supported = TRUE;

  if (dev_caps.pre_analysis) {
    amf_bool pre_analysis = FALSE;
    // Store initial pre-analysis value
    result =
        comp->GetProperty (AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE,
        &pre_analysis);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (device, "Failed to get pre-analysis option");
    }
    // We need to enable pre-analysis for checking options availability
    result =
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE,
        (amf_bool) TRUE);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (device, "Failed to set pre-analysis option");
    }
    gst_amf_encoder_check_pa_supported_options (&dev_caps.pa_supported, comp);
    result =
        comp->SetProperty (AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE,
        pre_analysis);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (device, "Failed to set pre-analysis option");
    }
  }

  {
    const AMFPropertyInfo *pinfo = nullptr;
    result = comp->GetPropertyInfo (AMF_VIDEO_ENCODER_AV1_GOP_SIZE, &pinfo);
    if (result == AMF_OK && pinfo) {
      dev_caps.default_gop_size = AMFVariantGetInt64 (&pinfo->defaultValue);
      dev_caps.min_gop_size = AMFVariantGetInt64 (&pinfo->minValue);
      dev_caps.max_gop_size = AMFVariantGetInt64 (&pinfo->maxValue);
      GST_INFO_OBJECT (device, "gop-size: default %d, min %d, max %d",
          (guint) dev_caps.default_gop_size,
          (guint) dev_caps.min_gop_size, (guint) dev_caps.max_gop_size);
    } else {
      dev_caps.default_gop_size = 30;
      dev_caps.min_gop_size = 0;
      dev_caps.max_gop_size = G_MAXINT;
    }
  }

  min_width = MAX (in_min_width, 1);
  max_width = in_max_width;
  if (max_width == 0) {
    GST_WARNING_OBJECT (device, "Unknown max width, assuming 4096");
    max_width = 4096;
  }

  min_height = MAX (in_min_height, 1);
  max_height = in_max_height;
  if (max_height == 0) {
    GST_WARNING_OBJECT (device, "Unknown max height, assuming 4096");
    max_height = 4096;
  }

  resolution_str = "width = (int) [ " + std::to_string (min_width)
      + ", " + std::to_string (max_width) + " ]";
  resolution_str += ", height = (int) [ " + std::to_string (min_height)
      + ", " + std::to_string (max_height) + " ]";

  sink_caps_str = "video/x-raw, " + format_str + ", " + resolution_str;
  src_caps_str = "video/x-av1, " + resolution_str + ", profile = (string) main"
      ", stream-format = (string) obu-stream, alignment = (string) tu";

  system_caps = gst_caps_from_string (sink_caps_str.c_str ());
  sink_caps = gst_caps_copy (system_caps);
  gst_caps_set_features (sink_caps, 0,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
          nullptr));
  gst_caps_append (sink_caps, system_caps);

  cdata = g_new0 (GstAmfAv1EncClassData, 1);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = gst_caps_from_string (src_caps_str.c_str ());
  cdata->dev_caps = dev_caps;
  g_object_get (device, "adapter-luid", &cdata->adapter_luid, nullptr);

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GST_DEBUG_OBJECT (device, "Sink caps %" GST_PTR_FORMAT, cdata->sink_caps);
  GST_DEBUG_OBJECT (device, "Src caps %" GST_PTR_FORMAT, cdata->src_caps);

  return cdata;
}

void
gst_amf_av1_enc_register_d3d11 (GstPlugin * plugin, GstD3D11Device * device,
    gpointer context, guint rank)
{
  GstAmfAv1EncClassData *cdata;
  AMFContext *amf_context = (AMFContext *) context;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;

  GST_DEBUG_CATEGORY_INIT (gst_amf_av1_enc_debug, "amfav1enc", 0, "amfav1enc");

  result = factory->CreateComponent (amf_context, AMFVideoEncoder_AV1, &comp);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Failed to create component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return;
  }

  cdata = gst_amf_av1_enc_create_class_data (device, comp.GetPtr ());
  if (!cdata)
    return;

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstAmfAv1EncClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_amf_av1_enc_class_init,
    nullptr,
    cdata,
    sizeof (GstAmfAv1Enc),
    0,
    (GInstanceInitFunc) gst_amf_av1_enc_init,
  };

  type_name = g_strdup ("GstAmfAv1Enc");
  feature_name = g_strdup ("amfav1enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstAmfAv1Device%dEnc", index);
    feature_name = g_strdup_printf ("amfav1device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_AMF_ENCODER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
