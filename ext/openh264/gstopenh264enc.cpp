/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstopenh264enc.h"

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <string.h>

#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/codec_ver.h>

#define GST_OPENH264ENC_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_OPENH264ENC, GstOpenh264EncPrivate))

GST_DEBUG_CATEGORY_STATIC (gst_openh264enc_debug_category);
#define GST_CAT_DEFAULT gst_openh264enc_debug_category

#define GST_TYPE_USAGE_TYPE (gst_openh264enc_usage_type_get_type ())
static GType
gst_openh264enc_usage_type_get_type (void)
{
  static GType usage_type = 0;

  if (!usage_type) {
    static const GEnumValue usage_types[] = {
      {CAMERA_VIDEO_REAL_TIME, "video from camera", "camera"},
      {SCREEN_CONTENT_REAL_TIME, "screen content", "screen"},
      {0, NULL, NULL},
    };

    usage_type = g_enum_register_static ("EUsageType", usage_types);
  }

  return usage_type;
}

#define GST_TYPE_RC_MODES (gst_openh264enc_rc_modes_get_type ())
static GType
gst_openh264enc_rc_modes_get_type (void)
{
  static GType rc_modes_type = 0;

  if (!rc_modes_type) {
    static const GEnumValue rc_modes_types[] = {
      {RC_QUALITY_MODE, "Quality mode", "quality"},
      {RC_BITRATE_MODE, "Bitrate mode", "bitrate"},
      {RC_BUFFERBASED_MODE, "No bitrate control, just using buffer status", "buffer"},
      {RC_OFF_MODE, "Rate control off mode", "off"},
      {0, NULL, NULL},
    };

    rc_modes_type = g_enum_register_static ("RC_MODES", rc_modes_types);
  }

  return rc_modes_type;
}

typedef enum _GstOpenh264encDeblockingMode
{
  GST_OPENH264_DEBLOCKING_ON = 0,
  GST_OPENH264_DEBLOCKING_OFF = 1,
  GST_OPENH264_DEBLOCKING_NOT_SLICE_BOUNDARIES = 2
} GstOpenh264encDeblockingMode;

#define GST_TYPE_OPENH264ENC_DEBLOCKING_MODE (gst_openh264enc_deblocking_mode_get_type ())
static GType
gst_openh264enc_deblocking_mode_get_type (void)
{
  static const GEnumValue types[] = {
    {GST_OPENH264_DEBLOCKING_ON, "Deblocking on", "on"},
    {GST_OPENH264_DEBLOCKING_OFF, "Deblocking off", "off"},
    {GST_OPENH264_DEBLOCKING_NOT_SLICE_BOUNDARIES,
        "Deblocking on, except for slice boundaries", "not-slice-boundaries"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (& id)) {
    GType _id = g_enum_register_static ("GstOpenh264encDeblockingModes", types);
    g_once_init_leave (& id, _id);
  }

  return (GType) id;
}

#define GST_TYPE_OPENH264ENC_SLICE_MODE (gst_openh264enc_slice_mode_get_type ())
static GType
gst_openh264enc_slice_mode_get_type (void)
{
  static const GEnumValue types[] = {
    {SM_FIXEDSLCNUM_SLICE, "num-slices slices", "n-slices"},
    {SM_AUTO_SLICE, "Number of slices equal to number of threads", "auto"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (& id)) {
    GType _id = g_enum_register_static ("GstOpenh264encSliceModes", types);
    g_once_init_leave (& id, _id);
  }

  return (GType) id;
}

#define GST_TYPE_OPENH264ENC_COMPLEXITY (gst_openh264enc_complexity_get_type ())
static GType
gst_openh264enc_complexity_get_type (void)
{
  static const GEnumValue types[] = {
    {LOW_COMPLEXITY, "Low complexity / high speed encoding", "low"},
    {MEDIUM_COMPLEXITY, "Medium complexity / medium speed encoding", "medium"},
    {HIGH_COMPLEXITY, "High complexity / low speed encoding", "high"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (& id)) {
    GType _id = g_enum_register_static ("GstOpenh264encComplexity", types);
    g_once_init_leave (& id, _id);
  }

  return (GType) id;
}

/* prototypes */

static void gst_openh264enc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_openh264enc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_openh264enc_finalize (GObject * object);
static gboolean gst_openh264enc_start (GstVideoEncoder * encoder);
static gboolean gst_openh264enc_stop (GstVideoEncoder * encoder);
static gboolean gst_openh264enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_openh264enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_openh264enc_finish (GstVideoEncoder * encoder);
static gboolean gst_openh264enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static void gst_openh264enc_set_usage_type (GstOpenh264Enc * openh264enc,
    gint usage_type);
static void gst_openh264enc_set_rate_control (GstOpenh264Enc * openh264enc,
    gint rc_mode);


#define DEFAULT_BITRATE            (128000)
#define DEFAULT_GOP_SIZE           (90)
#define DEFAULT_MAX_SLICE_SIZE     (1500000)
#define DROP_BITRATE               20000
#define START_FRAMERATE            30
#define DEFAULT_USAGE_TYPE         CAMERA_VIDEO_REAL_TIME
#define DEFAULT_RATE_CONTROL       RC_QUALITY_MODE
#define DEFAULT_MULTI_THREAD       0
#define DEFAULT_ENABLE_DENOISE     FALSE
#define DEFAULT_ENABLE_FRAME_SKIP  FALSE
#define DEFAULT_DEBLOCKING_MODE GST_OPENH264_DEBLOCKING_ON
#define DEFAULT_BACKGROUND_DETECTION TRUE
#define DEFAULT_ADAPTIVE_QUANTIZATION TRUE
#define DEFAULT_SCENE_CHANGE_DETECTION TRUE
#define DEFAULT_SLICE_MODE      SM_FIXEDSLCNUM_SLICE
#define DEFAULT_NUM_SLICES      1
#define DEFAULT_COMPLEXITY      MEDIUM_COMPLEXITY

enum
{
  PROP_0,
  PROP_USAGE_TYPE,
  PROP_BITRATE,
  PROP_GOP_SIZE,
  PROP_MAX_SLICE_SIZE,
  PROP_RATE_CONTROL,
  PROP_MULTI_THREAD,
  PROP_ENABLE_DENOISE,
  PROP_ENABLE_FRAME_SKIP,
  PROP_DEBLOCKING_MODE,
  PROP_BACKGROUND_DETECTION,
  PROP_ADAPTIVE_QUANTIZATION,
  PROP_SCENE_CHANGE_DETECTION,
  PROP_SLICE_MODE,
  PROP_NUM_SLICES,
  PROP_COMPLEXITY,
  N_PROPERTIES
};

struct _GstOpenh264EncPrivate
{
  ISVCEncoder *encoder;
  EUsageType usage_type;
  guint gop_size;
  RC_MODES rate_control;
  guint max_slice_size;
  guint bitrate;
  guint framerate;
  guint multi_thread;
  gboolean enable_denoise;
  gboolean enable_frame_skip;
  GstVideoCodecState *input_state;
  guint32 drop_bitrate;
  guint64 time_per_frame;
  guint64 frame_count;
  guint64 previous_timestamp;
  GstOpenh264encDeblockingMode deblocking_mode;
  gboolean background_detection;
  gboolean adaptive_quantization;
  gboolean scene_change_detection;
  SliceModeEnum slice_mode;
  guint num_slices;
  ECOMPLEXITY_MODE complexity;
};

/* pad templates */

static GstStaticPadTemplate gst_openh264enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );

static GstStaticPadTemplate gst_openh264enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, stream-format=(string)\"avc\", alignment=(string)\"au\", profile=(string)\"baseline\"")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstOpenh264Enc, gst_openh264enc,
    GST_TYPE_VIDEO_ENCODER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL);
    GST_DEBUG_CATEGORY_INIT (gst_openh264enc_debug_category, "openh264enc", 0,
        "debug category for openh264enc element"));

static void
gst_openh264enc_class_init (GstOpenh264EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstOpenh264EncPrivate));

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_openh264enc_src_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_openh264enc_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "OpenH264 video encoder", "Encoder/Video", "OpenH264 video encoder",
      "Ericsson AB, http://www.ericsson.com");

  gobject_class->set_property = gst_openh264enc_set_property;
  gobject_class->get_property = gst_openh264enc_get_property;
  gobject_class->finalize = gst_openh264enc_finalize;
  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_openh264enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_openh264enc_stop);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_openh264enc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_openh264enc_handle_frame);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_openh264enc_propose_allocation);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_openh264enc_finish);

  /* define properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_USAGE_TYPE,
      g_param_spec_enum ("usage-type", "Usage type",
          "Type of video content",
          GST_TYPE_USAGE_TYPE, CAMERA_VIDEO_REAL_TIME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate control",
          "Rate control mode",
          GST_TYPE_RC_MODES, RC_QUALITY_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MULTI_THREAD,
      g_param_spec_uint ("multi-thread", "Number of threads",
          "The number of threads.",
          0, G_MAXUINT, DEFAULT_MULTI_THREAD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ENABLE_DENOISE,
      g_param_spec_boolean ("enable-denoise", "Denoise Control",
          "Denoise control",
          FALSE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ENABLE_FRAME_SKIP,
      g_param_spec_boolean ("enable-frame-skip", "Skip Frames",
          "Skip frames to reach target bitrate",
          FALSE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate (in bits per second)",
          0, G_MAXUINT, DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
      g_param_spec_uint ("gop-size", "GOP size",
          "Number of frames between intra frames",
          0, G_MAXUINT, DEFAULT_GOP_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MAX_SLICE_SIZE,
      g_param_spec_uint ("max-slice-size", "Max slice size",
          "The maximum size of one slice (in bytes).",
          0, G_MAXUINT, DEFAULT_MAX_SLICE_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DEBLOCKING_MODE, g_param_spec_enum ("deblocking",
          "Deblocking mode", "Deblocking mode",
          GST_TYPE_OPENH264ENC_DEBLOCKING_MODE, DEFAULT_DEBLOCKING_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BACKGROUND_DETECTION,
      g_param_spec_boolean ("background-detection", "Background detection",
          "Background detection", DEFAULT_BACKGROUND_DETECTION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ADAPTIVE_QUANTIZATION,
      g_param_spec_boolean ("adaptive-quantization", "Adaptive quantization",
          "Adaptive quantization", DEFAULT_ADAPTIVE_QUANTIZATION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SCENE_CHANGE_DETECTION,
      g_param_spec_boolean ("scene-change-detection",
          "Scene change detection", "Scene change detection",
          DEFAULT_SCENE_CHANGE_DETECTION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "Slice mode", "Slice mode",
          GST_TYPE_OPENH264ENC_SLICE_MODE, DEFAULT_SLICE_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices", "Number of slices",
          "The number of slices (needs slice-mode=n-slices)",
          0, G_MAXUINT, DEFAULT_NUM_SLICES,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COMPLEXITY,
      g_param_spec_enum ("complexity", "Complexity / quality / speed tradeoff", "Complexity",
          GST_TYPE_OPENH264ENC_COMPLEXITY, DEFAULT_COMPLEXITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_openh264enc_init (GstOpenh264Enc * openh264enc)
{
  openh264enc->priv = GST_OPENH264ENC_GET_PRIVATE (openh264enc);
  openh264enc->priv->gop_size = DEFAULT_GOP_SIZE;
  openh264enc->priv->usage_type = DEFAULT_USAGE_TYPE;
  openh264enc->priv->rate_control = DEFAULT_RATE_CONTROL;
  openh264enc->priv->multi_thread = DEFAULT_MULTI_THREAD;
  openh264enc->priv->max_slice_size = DEFAULT_MAX_SLICE_SIZE;
  openh264enc->priv->bitrate = DEFAULT_BITRATE;
  openh264enc->priv->framerate = START_FRAMERATE;
  openh264enc->priv->input_state = NULL;
  openh264enc->priv->time_per_frame = GST_SECOND / openh264enc->priv->framerate;
  openh264enc->priv->frame_count = 0;
  openh264enc->priv->previous_timestamp = 0;
  openh264enc->priv->drop_bitrate = DROP_BITRATE;
  openh264enc->priv->enable_denoise = DEFAULT_ENABLE_DENOISE;
  openh264enc->priv->enable_frame_skip = DEFAULT_ENABLE_FRAME_SKIP;
  openh264enc->priv->deblocking_mode = DEFAULT_DEBLOCKING_MODE;
  openh264enc->priv->background_detection = DEFAULT_BACKGROUND_DETECTION;
  openh264enc->priv->adaptive_quantization = DEFAULT_ADAPTIVE_QUANTIZATION;
  openh264enc->priv->scene_change_detection = DEFAULT_SCENE_CHANGE_DETECTION;
  openh264enc->priv->slice_mode = DEFAULT_SLICE_MODE;
  openh264enc->priv->num_slices = DEFAULT_NUM_SLICES;
  openh264enc->priv->encoder = NULL;
  openh264enc->priv->complexity = DEFAULT_COMPLEXITY;
  gst_openh264enc_set_usage_type (openh264enc, CAMERA_VIDEO_REAL_TIME);
  gst_openh264enc_set_rate_control (openh264enc, RC_QUALITY_MODE);
}

static void
gst_openh264enc_set_usage_type (GstOpenh264Enc * openh264enc, gint usage_type)
{
  switch (usage_type) {
    case CAMERA_VIDEO_REAL_TIME:
      openh264enc->priv->usage_type = CAMERA_VIDEO_REAL_TIME;
      break;
    case SCREEN_CONTENT_REAL_TIME:
      openh264enc->priv->usage_type = SCREEN_CONTENT_REAL_TIME;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_openh264enc_set_rate_control (GstOpenh264Enc * openh264enc, gint rc_mode)
{
  switch (rc_mode) {
    case RC_QUALITY_MODE:
      openh264enc->priv->rate_control = RC_QUALITY_MODE;
      break;
    case RC_BITRATE_MODE:
      openh264enc->priv->rate_control = RC_BITRATE_MODE;
      break;
    case RC_BUFFERBASED_MODE:
      openh264enc->priv->rate_control = RC_BUFFERBASED_MODE;
      break;
    case RC_OFF_MODE:
      openh264enc->priv->rate_control = RC_OFF_MODE;
      break;
    default:
      g_assert_not_reached ();
  }
}

void
gst_openh264enc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (object);

  GST_DEBUG_OBJECT (openh264enc, "set_property");

  switch (property_id) {
    case PROP_BITRATE:
      openh264enc->priv->bitrate = g_value_get_uint (value);
      break;

    case PROP_MULTI_THREAD:
      openh264enc->priv->multi_thread = g_value_get_uint (value);
      break;

    case PROP_USAGE_TYPE:
      gst_openh264enc_set_usage_type (openh264enc, g_value_get_enum (value));
      break;

    case PROP_ENABLE_DENOISE:
      openh264enc->priv->enable_denoise = g_value_get_boolean (value);
      break;

    case PROP_ENABLE_FRAME_SKIP:
      openh264enc->priv->enable_frame_skip = g_value_get_boolean (value);
      break;

    case PROP_RATE_CONTROL:
      gst_openh264enc_set_rate_control (openh264enc, g_value_get_enum (value));
      break;

    case PROP_GOP_SIZE:
      openh264enc->priv->gop_size = g_value_get_uint (value);
      break;

    case PROP_MAX_SLICE_SIZE:
      openh264enc->priv->max_slice_size = g_value_get_uint (value);
      break;

    case PROP_DEBLOCKING_MODE:
      openh264enc->priv->deblocking_mode =
          (GstOpenh264encDeblockingMode) g_value_get_enum (value);
      break;

    case PROP_BACKGROUND_DETECTION:
      openh264enc->priv->background_detection = g_value_get_boolean (value);
      break;

    case PROP_ADAPTIVE_QUANTIZATION:
      openh264enc->priv->adaptive_quantization = g_value_get_boolean (value);
      break;

    case PROP_SCENE_CHANGE_DETECTION:
      openh264enc->priv->scene_change_detection = g_value_get_boolean (value);
      break;

    case PROP_SLICE_MODE:
      openh264enc->priv->slice_mode = (SliceModeEnum) g_value_get_enum (value);
      break;

    case PROP_NUM_SLICES:
      openh264enc->priv->num_slices = g_value_get_uint (value);
      break;

    case PROP_COMPLEXITY:
      openh264enc->priv->complexity = (ECOMPLEXITY_MODE) g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_openh264enc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (object);

  GST_DEBUG_OBJECT (openh264enc, "get_property");

  switch (property_id) {
    case PROP_USAGE_TYPE:
      g_value_set_enum (value, openh264enc->priv->usage_type);
      break;

    case PROP_RATE_CONTROL:
      g_value_set_enum (value, openh264enc->priv->rate_control);
      break;

    case PROP_BITRATE:
      g_value_set_uint (value, openh264enc->priv->bitrate);
      break;

    case PROP_ENABLE_DENOISE:
      g_value_set_boolean (value, openh264enc->priv->enable_denoise);
      break;

    case PROP_ENABLE_FRAME_SKIP:
      g_value_set_boolean (value, openh264enc->priv->enable_frame_skip);
      break;

    case PROP_MULTI_THREAD:
      g_value_set_uint (value, openh264enc->priv->multi_thread);
      break;

    case PROP_GOP_SIZE:
      g_value_set_uint (value, openh264enc->priv->gop_size);
      break;

    case PROP_MAX_SLICE_SIZE:
      g_value_set_uint (value, openh264enc->priv->max_slice_size);
      break;

    case PROP_DEBLOCKING_MODE:
      g_value_set_enum (value, openh264enc->priv->deblocking_mode);
      break;

    case PROP_BACKGROUND_DETECTION:
      g_value_set_boolean (value, openh264enc->priv->background_detection);
      break;

    case PROP_ADAPTIVE_QUANTIZATION:
      g_value_set_boolean (value, openh264enc->priv->adaptive_quantization);
      break;

    case PROP_SCENE_CHANGE_DETECTION:
      g_value_set_boolean (value, openh264enc->priv->scene_change_detection);
      break;

    case PROP_SLICE_MODE:
      g_value_set_enum (value, openh264enc->priv->slice_mode);
      break;

    case PROP_NUM_SLICES:
      g_value_set_uint (value, openh264enc->priv->num_slices);
      break;

    case PROP_COMPLEXITY:
      g_value_set_enum (value, openh264enc->priv->complexity);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_openh264enc_finalize (GObject * object)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (object);

  GST_DEBUG_OBJECT (openh264enc, "finalize");

  /* clean up object here */

  if (openh264enc->priv->input_state) {
    gst_video_codec_state_unref (openh264enc->priv->input_state);
  }
  openh264enc->priv->input_state = NULL;

  G_OBJECT_CLASS (gst_openh264enc_parent_class)->finalize (object);
}

static gboolean
gst_openh264enc_start (GstVideoEncoder * encoder)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (encoder);
  GST_DEBUG_OBJECT (openh264enc, "start");

  return TRUE;
}

static gboolean
gst_openh264enc_stop (GstVideoEncoder * encoder)
{
  GstOpenh264Enc *openh264enc;

  openh264enc = GST_OPENH264ENC (encoder);

  if (openh264enc->priv->encoder != NULL) {
    openh264enc->priv->encoder->Uninitialize ();
    WelsDestroySVCEncoder (openh264enc->priv->encoder);
    openh264enc->priv->encoder = NULL;
  }
  openh264enc->priv->encoder = NULL;

  if (openh264enc->priv->input_state) {
    gst_video_codec_state_unref (openh264enc->priv->input_state);
  }
  openh264enc->priv->input_state = NULL;

  GST_DEBUG_OBJECT (openh264enc, "openh264_enc_stop called");

  return TRUE;
}


static gboolean
gst_openh264enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (encoder);
  GstOpenh264EncPrivate *priv = openh264enc->priv;
  gchar *debug_caps;
  SFrameBSInfo bsInfo;
  guint width, height, fps_n, fps_d;
  SEncParamExt enc_params;
  gint ret;
  guchar *nal_sps_data = NULL;
  gint nal_sps_length = 0;
  guchar *nal_pps_data = NULL;
  gint nal_pps_length = 0;
  guchar *sps_tmp_buf;
  guchar *codec_data_tmp_buf;
  GstBuffer *codec_data;
  GstCaps *outcaps;
  GstVideoCodecState *output_state;
  openh264enc->priv->frame_count = 0;
  int video_format = videoFormatI420;

  debug_caps = gst_caps_to_string (state->caps);
  GST_DEBUG_OBJECT (openh264enc, "gst_e26d4_enc_set_format called, caps: %s",
      debug_caps);
  g_free (debug_caps);

  gst_openh264enc_stop (encoder);

  if (priv->input_state) {
    gst_video_codec_state_unref (priv->input_state);
  }
  priv->input_state = gst_video_codec_state_ref (state);

  width = GST_VIDEO_INFO_WIDTH (&state->info);
  height = GST_VIDEO_INFO_HEIGHT (&state->info);
  fps_n = GST_VIDEO_INFO_FPS_N (&state->info);
  fps_d = GST_VIDEO_INFO_FPS_D (&state->info);

  if (priv->encoder != NULL) {
    priv->encoder->Uninitialize ();
    WelsDestroySVCEncoder (priv->encoder);
    priv->encoder = NULL;
  }
  WelsCreateSVCEncoder (&(priv->encoder));
  unsigned int uiTraceLevel = WELS_LOG_ERROR;
  priv->encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &uiTraceLevel);

  priv->encoder->GetDefaultParams (&enc_params);

  enc_params.iUsageType = openh264enc->priv->usage_type;
  enc_params.iPicWidth = width;
  enc_params.iPicHeight = height;
  enc_params.iTargetBitrate = openh264enc->priv->bitrate;
  enc_params.iRCMode = RC_QUALITY_MODE;
  enc_params.iTemporalLayerNum = 1;
  enc_params.iSpatialLayerNum = 1;
  enc_params.iLtrMarkPeriod = 30;
  enc_params.iMultipleThreadIdc = openh264enc->priv->multi_thread;
  enc_params.bEnableDenoise = openh264enc->priv->enable_denoise;
  enc_params.iComplexityMode = priv->complexity;
  enc_params.uiIntraPeriod = priv->gop_size;
  enc_params.bEnableBackgroundDetection =
      openh264enc->priv->background_detection;
  enc_params.bEnableAdaptiveQuant = openh264enc->priv->adaptive_quantization;
  enc_params.bEnableSceneChangeDetect =
      openh264enc->priv->scene_change_detection;
  enc_params.bEnableFrameSkip = openh264enc->priv->enable_frame_skip;
  enc_params.bEnableLongTermReference = 0;
#if OPENH264_MINOR >= 4
  enc_params.eSpsPpsIdStrategy = CONSTANT_ID;
#else
  enc_params.bEnableSpsPpsIdAddition = 1;
#endif
  enc_params.bPrefixNalAddingCtrl = 0;
  enc_params.fMaxFrameRate = fps_n * 1.0 / fps_d;
  enc_params.iLoopFilterDisableIdc = openh264enc->priv->deblocking_mode;
  enc_params.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
  enc_params.sSpatialLayers[0].iVideoWidth = width;
  enc_params.sSpatialLayers[0].iVideoHeight = height;
  enc_params.sSpatialLayers[0].fFrameRate = fps_n * 1.0 / fps_d;
  enc_params.sSpatialLayers[0].iSpatialBitrate = openh264enc->priv->bitrate;
  enc_params.sSpatialLayers[0].sSliceCfg.uiSliceMode =
      openh264enc->priv->slice_mode;
  enc_params.sSpatialLayers[0].sSliceCfg.sSliceArgument.uiSliceNum =
      openh264enc->priv->num_slices;

  priv->framerate = (1 + fps_n / fps_d);

  ret = priv->encoder->InitializeExt (&enc_params);

  if (ret != cmResultSuccess) {
    GST_ERROR_OBJECT (openh264enc, "failed to initialize encoder");
    return FALSE;
  }

  priv->encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);

  memset (&bsInfo, 0, sizeof (SFrameBSInfo));

  ret = priv->encoder->EncodeParameterSets (&bsInfo);

  nal_sps_data = bsInfo.sLayerInfo[0].pBsBuf + 4;
  nal_sps_length = bsInfo.sLayerInfo[0].pNalLengthInByte[0] - 4;

  nal_pps_data = bsInfo.sLayerInfo[0].pBsBuf + nal_sps_length + 8;
  nal_pps_length = bsInfo.sLayerInfo[0].pNalLengthInByte[1] - 4;

  if (ret != cmResultSuccess) {
    GST_ELEMENT_ERROR (openh264enc, STREAM, ENCODE,
        ("Could not create headers"), ("Could not create SPS"));
    return FALSE;
  }

  sps_tmp_buf = (guchar *) (g_memdup (nal_sps_data, nal_sps_length));

  codec_data_tmp_buf =
      (guchar *) g_malloc (5 + 3 + nal_sps_length + 3 + nal_pps_length);
  codec_data_tmp_buf[0] = 1; /* version 1 */ ;
  codec_data_tmp_buf[1] = sps_tmp_buf[1];       /* profile */
  codec_data_tmp_buf[2] = sps_tmp_buf[2];       /* profile constraints */
  codec_data_tmp_buf[3] = sps_tmp_buf[3];       /* level */
  codec_data_tmp_buf[4] = 1;    /* NAL length marker length minus one */
  codec_data_tmp_buf[5] = 1;    /* Number of SPS */
  GST_WRITE_UINT16_BE (codec_data_tmp_buf + 6, nal_sps_length);
  memcpy (codec_data_tmp_buf + 8, sps_tmp_buf, nal_sps_length);

  g_free (sps_tmp_buf);

  codec_data_tmp_buf[8 + nal_sps_length] = 1;   /* Number of PPS */
  GST_WRITE_UINT16_BE (codec_data_tmp_buf + 8 + nal_sps_length + 1,
      nal_pps_length);
  memcpy (codec_data_tmp_buf + 8 + nal_sps_length + 3, nal_pps_data,
      nal_pps_length);

  GST_DEBUG_OBJECT (openh264enc, "Got SPS of size %d and PPS of size %d",
      nal_sps_length, nal_pps_length);

  codec_data =
      gst_buffer_new_wrapped (codec_data_tmp_buf,
      5 + 3 + nal_sps_length + 3 + nal_pps_length);

  outcaps =
      gst_caps_copy (gst_static_pad_template_get_caps
      (&gst_openh264enc_src_template));
  gst_caps_set_simple (outcaps, "codec_data", GST_TYPE_BUFFER, codec_data,
      NULL);
  gst_buffer_unref (codec_data);

  output_state = gst_video_encoder_set_output_state (encoder, outcaps, state);
  gst_video_codec_state_unref (output_state);

  return gst_video_encoder_negotiate (encoder);
}

static gboolean
gst_openh264enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return
      GST_VIDEO_ENCODER_CLASS
      (gst_openh264enc_parent_class)->propose_allocation (encoder, query);
}

static GstFlowReturn
gst_openh264enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (encoder);
  SSourcePicture *src_pic = NULL;
  GstVideoFrame video_frame;
  gboolean force_keyframe;
  gint ret;
  SFrameBSInfo frame_info;
  gfloat fps;
  GstVideoEncoder *base_encoder = GST_VIDEO_ENCODER (openh264enc);

  if (frame) {
    src_pic = new SSourcePicture;

    if (src_pic == NULL) {
      if (frame)
        gst_video_codec_frame_unref (frame);
      return GST_FLOW_ERROR;
    }
    //fill default src_pic
    src_pic->iColorFormat = videoFormatI420;
    src_pic->uiTimeStamp = 0;
  }

  openh264enc->priv->frame_count++;
  if (frame) {
    if (G_UNLIKELY (openh264enc->priv->frame_count == 1)) {
      openh264enc->priv->time_per_frame =
          (GST_NSECOND / openh264enc->priv->framerate);
      openh264enc->priv->previous_timestamp = frame->pts;
    } else {
      openh264enc->priv->time_per_frame =
          openh264enc->priv->time_per_frame * 0.8 + (frame->pts -
          openh264enc->priv->previous_timestamp) * 0.2;
      openh264enc->priv->previous_timestamp = frame->pts;
      if (openh264enc->priv->frame_count % 10 == 0) {
        fps = GST_SECOND / (gdouble) openh264enc->priv->time_per_frame;
        openh264enc->priv->encoder->SetOption (ENCODER_OPTION_FRAME_RATE, &fps);
      }
    }
  }

  if (openh264enc->priv->bitrate <= openh264enc->priv->drop_bitrate) {
    GST_LOG_OBJECT (openh264enc, "Dropped frame due to too low bitrate");
    if (frame) {
      gst_video_encoder_finish_frame (encoder, frame);
      delete src_pic;
    }
    return GST_FLOW_OK;
  }

  if (frame) {
    gst_video_frame_map (&video_frame, &openh264enc->priv->input_state->info,
        frame->input_buffer, GST_MAP_READ);
    src_pic->iPicWidth = GST_VIDEO_FRAME_WIDTH (&video_frame);
    src_pic->iPicHeight = GST_VIDEO_FRAME_HEIGHT (&video_frame);
    src_pic->iStride[0] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 0);
    src_pic->iStride[1] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 1);
    src_pic->iStride[2] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 2);
    src_pic->pData[0] = GST_VIDEO_FRAME_COMP_DATA (&video_frame, 0);
    src_pic->pData[1] = GST_VIDEO_FRAME_COMP_DATA (&video_frame, 1);
    src_pic->pData[2] = GST_VIDEO_FRAME_COMP_DATA (&video_frame, 2);

    force_keyframe = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame);
    if (force_keyframe) {
      openh264enc->priv->encoder->ForceIntraFrame (true);
      GST_DEBUG_OBJECT (openh264enc,
          "Got force key unit event, next frame coded as intra picture");
    }
  }

  memset (&frame_info, 0, sizeof (SFrameBSInfo));
  ret = openh264enc->priv->encoder->EncodeFrame (src_pic, &frame_info);
  if (ret != cmResultSuccess) {
    if (frame) {
      gst_video_frame_unmap (&video_frame);
      gst_video_codec_frame_unref (frame);
      delete src_pic;
      GST_ELEMENT_ERROR (openh264enc, STREAM, ENCODE,
          ("Could not encode frame"), ("Openh264 returned %d", ret));
      return GST_FLOW_ERROR;
    } else {
      return GST_FLOW_EOS;
    }
  }

  if (videoFrameTypeSkip == frame_info.eFrameType) {
    if (frame) {
      gst_video_frame_unmap (&video_frame);
      gst_video_encoder_finish_frame (base_encoder, frame);
      delete src_pic;
    }

    return GST_FLOW_OK;
  }

  if (frame) {
    gst_video_frame_unmap (&video_frame);
    gst_video_codec_frame_unref (frame);
    delete src_pic;
    src_pic = NULL;
    frame = NULL;
  }

  /* FIXME: openh264 has no way for us to get a connection
   * between the input and output frames, we just have to
   * guess based on the input */
  frame = gst_video_encoder_get_oldest_frame (base_encoder);
  if (!frame) {
    GST_ELEMENT_ERROR (openh264enc, STREAM, ENCODE,
        ("Could not encode frame"), ("openh264enc returned %d", ret));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  SLayerBSInfo *bs_info = &frame_info.sLayerInfo[0];
  gint nal_size = bs_info->pNalLengthInByte[0] - 4;
  guchar *nal_sps_data, *nal_pps_data;
  gint nal_sps_length, nal_pps_length, idr_length, tmp_buf_length;

  if (videoFrameTypeIDR == frame_info.eFrameType) {
    GstMapInfo map;

    /* sps */
    nal_sps_data = frame_info.sLayerInfo[0].pBsBuf + 4;
    nal_sps_length = frame_info.sLayerInfo[0].pNalLengthInByte[0] - 4;
    /* pps */
    nal_pps_data = nal_sps_data + frame_info.sLayerInfo[0].pNalLengthInByte[0];
    nal_pps_length = frame_info.sLayerInfo[0].pNalLengthInByte[1] - 4;
    /* idr */
    bs_info = &frame_info.sLayerInfo[1];
    idr_length = bs_info->pNalLengthInByte[0] - 4;

    tmp_buf_length = nal_sps_length + 2 + nal_pps_length + 2 + idr_length + 2;
    frame->output_buffer =
        gst_video_encoder_allocate_output_buffer (encoder, tmp_buf_length);
    gst_buffer_map (frame->output_buffer, &map, GST_MAP_WRITE);

    GST_WRITE_UINT16_BE (map.data, nal_sps_length);
    memcpy (map.data + 2, nal_sps_data, nal_sps_length);

    GST_WRITE_UINT16_BE (map.data + nal_sps_length + 2, nal_pps_length);
    memcpy (map.data + nal_sps_length + 2 + 2, nal_pps_data, nal_pps_length);

    GST_WRITE_UINT16_BE (map.data + nal_sps_length + 2 + nal_pps_length + 2,
        idr_length);
    memcpy (map.data + nal_sps_length + 2 + nal_pps_length + 2 + 2,
        bs_info->pBsBuf + 4, idr_length);

    gst_buffer_unmap (frame->output_buffer, &map);

    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  } else {
    GstMapInfo map;

    tmp_buf_length = nal_size + 2;
    frame->output_buffer =
        gst_video_encoder_allocate_output_buffer (encoder, tmp_buf_length);
    gst_buffer_map (frame->output_buffer, &map, GST_MAP_WRITE);

    GST_WRITE_UINT16_BE (map.data, nal_size);
    memcpy (map.data + 2, bs_info->pBsBuf + 4, nal_size);

    gst_buffer_unmap (frame->output_buffer, &map);

    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
  }

  GST_LOG_OBJECT (openh264enc, "openh264 picture %scoded OK!",
      (ret != cmResultSuccess) ? "NOT " : "");

  return gst_video_encoder_finish_frame (encoder, frame);
}

static GstFlowReturn
gst_openh264enc_finish (GstVideoEncoder * encoder)
{
  GstOpenh264Enc *openh264enc = GST_OPENH264ENC (encoder);

  if (openh264enc->priv->frame_count == 0)
    return GST_FLOW_OK;

  /* Drain encoder */
  while ((gst_openh264enc_handle_frame (encoder, NULL)) == GST_FLOW_OK);

  return GST_FLOW_OK;
}
