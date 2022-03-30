/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamfh265enc.h"
#include <components/Component.h>
#include <components/VideoEncoderHEVC.h>
#include <core/Factory.h>
#include <string>
#include <vector>
#include <string.h>

using namespace amf;

GST_DEBUG_CATEGORY_STATIC (gst_amf_h265_enc_debug);
#define GST_CAT_DEFAULT gst_amf_h265_enc_debug

static GTypeClass *parent_class = nullptr;

typedef struct
{
  amf_int64 max_bitrate;
  amf_int64 num_of_streams;
  amf_int64 max_profile;
  amf_int64 max_tier;
  amf_int64 max_level;
  amf_int64 min_ref_frames;
  amf_int64 max_ref_frames;
  amf_int64 num_of_hw_instances;
  amf_int64 color_conversion;
  amf_int64 pre_analysis;
  amf_int64 roi_map;
  amf_int64 max_throughput;
  amf_int64 query_timeout_support;
  amf_int64 default_qp_i;
  amf_int64 default_qp_p;
  amf_int64 min_gop_size;
  amf_int64 max_gop_size;
  amf_int64 default_gop_size;
  guint valign;
} GstAmfH265EncDeviceCaps;

#define GST_TYPE_AMF_H265_ENC_USAGE (gst_amf_h265_enc_usage_get_type ())
static GType
gst_amf_h265_enc_usage_get_type (void)
{
  static GType usage_type = 0;
  static const GEnumValue usages[] = {
    {AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING, "Transcoding", "transcoding"},
    {AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY, "Ultra Low Latency",
        "ultra-low-latency"},
    {AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY, "Low Latency", "low-latency"},
    {AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM, "Webcam", "webcam"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&usage_type)) {
    GType type = g_enum_register_static ("GstAmfH265EncUsage", usages);
    g_once_init_leave (&usage_type, type);
  }

  return usage_type;
}

#define GST_TYPE_AMF_H265_ENC_RATE_CONTROL (gst_amf_h265_enc_rate_control_get_type ())
static GType
gst_amf_h265_enc_rate_control_get_type (void)
{
  static GType rate_control_type = 0;
  static const GEnumValue rate_controls[] = {
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN,
          "Default, depends on Usage",
        "default"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP, "Constant QP",
        "cqp"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
        "Latency Constrained VBR", "lcvbr"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
        "Peak Constrained VBR", "vbr"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR, "Constant Bitrate", "cbr"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&rate_control_type)) {
    GType type =
        g_enum_register_static ("GstAmfH265EncRateControl", rate_controls);
    g_once_init_leave (&rate_control_type, type);
  }

  return rate_control_type;
}

#define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_UNKNOWN -1

#define GST_TYPE_AMF_H265_ENC_PRESET (gst_amf_h265_enc_preset_get_type ())
static GType
gst_amf_h265_enc_preset_get_type (void)
{
  static GType preset_type = 0;
  static const GEnumValue presets[] = {
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_UNKNOWN, "Default, depends on USAGE",
        "default"},
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY, "Quality", "quality"},
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED, "Balanced", "balanced"},
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED, "Speed", "speed"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&preset_type)) {
    GType type = g_enum_register_static ("GstAmfH265EncPreset", presets);
    g_once_init_leave (&preset_type, type);
  }

  return preset_type;
}

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;

  gint64 adapter_luid;

  GstAmfH265EncDeviceCaps dev_caps;
} GstAmfH265EncClassData;

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
  PROP_AUD,
};

#define DEFAULT_USAGE AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING
#define DEFAULT_RATE_CONTROL AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN
#define DEFAULT_PRESET AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_UNKNOWN
#define DEFAULT_BITRATE 0
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_MIN_MAX_QP -1
#define DEFAULT_AUD TRUE

typedef struct _GstAmfH265Enc
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

  gboolean aud;
} GstAmfH265Enc;

typedef struct _GstAmfH265EncClass
{
  GstAmfEncoderClass parent_class;
  GstAmfH265EncDeviceCaps dev_caps;

  gint64 adapter_luid;
} GstAmfH265EncClass;

#define GST_AMF_H265_ENC(object) ((GstAmfH265Enc *) (object))
#define GST_AMF_H265_ENC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstAmfH265EncClass))

static void gst_amf_h265_enc_finalize (GObject * object);
static void gst_amf_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amf_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_amf_h265_enc_set_format (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component);
static gboolean gst_amf_h265_enc_set_output_state (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component);
static gboolean gst_amf_h265_enc_set_surfrace_prop (GstAmfEncoder * encoder,
    GstVideoCodecFrame * frame, gpointer surface);
static GstBuffer *gst_amf_h265_enc_create_output_buffer (GstAmfEncoder *
    encoder, gpointer data, gboolean * sync_point);
static gboolean gst_amf_h265_enc_check_reconfigure (GstAmfEncoder * encoder);

static void
gst_amf_h265_enc_class_init (GstAmfH265EncClass * klass, gpointer data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAmfEncoderClass *amf_class = GST_AMF_ENCODER_CLASS (klass);
  GstAmfH265EncClassData *cdata = (GstAmfH265EncClassData *) data;
  GstAmfH265EncDeviceCaps *dev_caps = &cdata->dev_caps;
  GParamFlags param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_MUTABLE_PLAYING | G_PARAM_STATIC_STRINGS);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  object_class->finalize = gst_amf_h265_enc_finalize;
  object_class->set_property = gst_amf_h265_enc_set_property;
  object_class->get_property = gst_amf_h265_enc_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of associated GPU",
          G_MININT64, G_MAXINT64, cdata->adapter_luid, param_flags));
  g_object_class_install_property (object_class, PROP_USAGE,
      g_param_spec_enum ("usage", "Usage",
          "Target usage", GST_TYPE_AMF_H265_ENC_USAGE,
          DEFAULT_USAGE, param_flags));
  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Rate Control Method", GST_TYPE_AMF_H265_ENC_RATE_CONTROL,
          DEFAULT_RATE_CONTROL, param_flags));
  g_object_class_install_property (object_class, PROP_PRESET,
      g_param_spec_enum ("preset", "Preset",
          "Preset", GST_TYPE_AMF_H265_ENC_PRESET, DEFAULT_PRESET, param_flags));
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
          -1, 51, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_QP_I,
      g_param_spec_int ("max-qp-i", "Max QP I",
          "Maximum allowed QP value for I frames (-1: USAGE default)",
          -1, 51, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MIN_QP_P,
      g_param_spec_int ("min-qp-p", "Min QP P",
          "Minimum allowed QP value for P frames (-1: USAGE default)",
          -1, 51, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_QP_P,
      g_param_spec_int ("max-qp-p", "Max QP P",
          "Maximum allowed QP value for P frames (-1: USAGE default)",
          -1, 51, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_I,
      g_param_spec_uint ("qp-i", "QP I",
          "Constant QP for I frames", 0, 51,
          (guint) dev_caps->default_qp_i, param_flags));
  g_object_class_install_property (object_class, PROP_QP_P,
      g_param_spec_uint ("qp-p", "QP P",
          "Constant QP for P frames", 0, 51,
          (guint) dev_caps->default_qp_p, param_flags));
  g_object_class_install_property (object_class, PROP_REF_FRAMES,
      g_param_spec_uint ("ref-frames", "Reference Frames",
          "Number of reference frames", (guint) dev_caps->min_ref_frames,
          (guint) dev_caps->max_ref_frames,
          (guint) dev_caps->min_ref_frames, param_flags));
  g_object_class_install_property (object_class, PROP_AUD,
      g_param_spec_boolean ("aud", "AUD",
          "Use AU (Access Unit) delimiter", DEFAULT_AUD, param_flags));

  gst_element_class_set_metadata (element_class,
      "AMD AMF H.265 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode H.265 video streams using AMF API",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  amf_class->set_format = GST_DEBUG_FUNCPTR (gst_amf_h265_enc_set_format);
  amf_class->set_output_state =
      GST_DEBUG_FUNCPTR (gst_amf_h265_enc_set_output_state);
  amf_class->set_surface_prop =
      GST_DEBUG_FUNCPTR (gst_amf_h265_enc_set_surfrace_prop);
  amf_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_amf_h265_enc_create_output_buffer);
  amf_class->check_reconfigure =
      GST_DEBUG_FUNCPTR (gst_amf_h265_enc_check_reconfigure);

  klass->dev_caps = cdata->dev_caps;
  klass->adapter_luid = cdata->adapter_luid;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_amf_h265_enc_init (GstAmfH265Enc * self)
{
  GstAmfH265EncClass *klass = GST_AMF_H265_ENC_GET_CLASS (self);
  GstAmfH265EncDeviceCaps *dev_caps = &klass->dev_caps;

  gst_amf_encoder_set_subclass_data (GST_AMF_ENCODER (self),
      klass->adapter_luid, AMFVideoEncoder_HEVC);

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
  self->ref_frames = (guint) dev_caps->min_ref_frames;
  self->aud = DEFAULT_AUD;
}

static void
gst_amf_h265_enc_finalize (GObject * object)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (object);

  g_mutex_clear (&self->prop_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_int (GstAmfH265Enc * self, gint * old_val, const GValue * new_val)
{
  gint val = g_value_get_int (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_uint (GstAmfH265Enc * self, guint * old_val, const GValue * new_val)
{
  guint val = g_value_get_uint (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_enum (GstAmfH265Enc * self, gint * old_val, const GValue * new_val)
{
  gint val = g_value_get_enum (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_bool (GstAmfH265Enc * self, gboolean * old_val, const GValue * new_val)
{
  gboolean val = g_value_get_boolean (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
gst_amf_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (object);

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
    case PROP_AUD:
      /* This is per frame property, don't need to reset encoder */
      self->aud = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&self->prop_lock);
}

static void
gst_amf_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmfH265EncClass *klass = GST_AMF_H265_ENC_GET_CLASS (object);
  GstAmfH265Enc *self = GST_AMF_H265_ENC (object);

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
    case PROP_AUD:
      g_value_set_boolean (value, self->aud);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_amf_h265_enc_set_format (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (encoder);
  AMFComponent *comp = (AMFComponent *) component;
  GstVideoInfo *info = &state->info;
  AMF_RESULT result;
  AMFRate framerate;
  AMFRatio aspect_ratio;
  amf_int64 int64_val;
  AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM rc_mode;

  g_mutex_lock (&self->prop_lock);
  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_FRAMESIZE,
      AMFConstructSize (info->width, info->height));
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set frame size, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_USAGE,
      (amf_int64) self->usage);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set usage, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_PROFILE,
      (amf_int64) AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set profile, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_MAX_NUM_REFRAMES,
      (amf_int64) self->ref_frames);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set ref-frames, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }


  aspect_ratio = AMFConstructRatio (info->par_n, info->par_d);
  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_ASPECT_RATIO,
      aspect_ratio);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set aspect ratio, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (info->colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255)
    int64_val = AMF_VIDEO_ENCODER_HEVC_NOMINAL_RANGE_FULL;
  else
    int64_val = AMF_VIDEO_ENCODER_HEVC_NOMINAL_RANGE_STUDIO;

  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_NOMINAL_RANGE, int64_val);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set full-range-color, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->Init (AMF_SURFACE_NV12, info->width, info->height);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to init component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (self->rate_control != AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
        (amf_int64) self->rate_control);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set rate-control, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  result = comp->GetProperty (AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
      &int64_val);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to get rate-control method, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  rc_mode = (AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM) int64_val;
  if (self->min_qp_i >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_MIN_QP_I,
        (amf_int64) self->min_qp_i);
  }
  if (self->max_qp_i >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_MAX_QP_I,
        (amf_int64) self->max_qp_i);
  }
  if (self->min_qp_p >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_MIN_QP_P,
        (amf_int64) self->min_qp_p);
  }
  if (self->max_qp_p >= 0) {
    comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_MAX_QP_P,
        (amf_int64) self->max_qp_p);
  }

  comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_QP_I, (amf_int64) self->qp_i);
  comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_QP_P, (amf_int64) self->qp_p);

  switch (rc_mode) {
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR:
      if (self->bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE,
            (amf_int64) self->bitrate * 1000);
        comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE,
            (amf_int64) self->bitrate * 1000);
      }
      break;
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR:
    case AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR:
      if (self->bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE,
            (amf_int64) self->bitrate * 1000);
      }
      if (self->max_bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE,
            (amf_int64) self->max_bitrate * 1000);
      }
      break;
    default:
      break;
  }

  /* Disable frame skip for now, need investigation the behavior */
  result =
      comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_SKIP_FRAME_ENABLE,
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

  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_FRAMERATE, framerate);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set frame rate, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_HEVC_GOP_SIZE,
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
gst_amf_h265_enc_set_output_state (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (encoder);
  GstVideoCodecState *output_state;
  GstCaps *caps;
  GstTagList *tags;

  caps = gst_caps_from_string ("video/x-h265, alignment = (string) au"
      ", stream-format = (string) byte-stream, profile = (string) main");
  output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      caps, state);

  GST_INFO_OBJECT (self, "Output caps: %" GST_PTR_FORMAT, output_state->caps);
  gst_video_codec_state_unref (output_state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      "amfh265enc", nullptr);

  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder),
      tags, GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static gboolean
gst_amf_h265_enc_set_surfrace_prop (GstAmfEncoder * encoder,
    GstVideoCodecFrame * frame, gpointer surface)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (encoder);
  AMFSurface *surf = (AMFSurface *) surface;
  AMF_RESULT result;
  amf_bool insert_aud = self->aud ? true : false;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    amf_int64 type = (amf_int64) AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR;
    result = surf->SetProperty (AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE,
        type);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (encoder, "Failed to set force idr, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    }
  }

  result = surf->SetProperty (AMF_VIDEO_ENCODER_HEVC_INSERT_AUD, &insert_aud);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (encoder, "Failed to set AUD, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
  }

  return TRUE;
}

static GstBuffer *
gst_amf_h265_enc_create_output_buffer (GstAmfEncoder * encoder,
    gpointer data, gboolean * sync_point)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (encoder);
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

  result = amf_buf->GetProperty (AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE,
      &output_type);
  if (result == AMF_OK &&
      output_type == (amf_int64) AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR) {
    *sync_point = TRUE;
  }

  return buf;
}

static gboolean
gst_amf_h265_enc_check_reconfigure (GstAmfEncoder * encoder)
{
  GstAmfH265Enc *self = GST_AMF_H265_ENC (encoder);
  gboolean ret;

  g_mutex_lock (&self->prop_lock);
  ret = self->property_updated;
  g_mutex_unlock (&self->prop_lock);

  return ret;
}

static GstAmfH265EncClassData *
gst_amf_h265_enc_create_class_data (GstD3D11Device * device,
    AMFComponent * comp)
{
  AMF_RESULT result;
  GstAmfH265EncDeviceCaps dev_caps = { 0, };
  std::string sink_caps_str;
  std::string src_caps_str;
  std::vector < std::string > profiles;
  std::string resolution_str;
  GstAmfH265EncClassData *cdata;
  AMFCapsPtr amf_caps;
  AMFIOCapsPtr in_iocaps;
  AMFIOCapsPtr out_iocaps;
  amf_int32 in_min_width = 0, in_max_width = 0;
  amf_int32 in_min_height = 0, in_max_height = 0;
  amf_int32 out_min_width = 0, out_max_width = 0;
  amf_int32 out_min_height = 0, out_max_height = 0;
  amf_int32 num_val;
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
  for (guint i = 0; i < num_val; i++) {
    AMF_SURFACE_FORMAT format;
    amf_bool native;

    result = in_iocaps->GetFormatAt (i, &format, &native);
    if (result != AMF_OK)
      continue;

    GST_INFO_OBJECT (device, "Format %d supported, native %d", format, native);
    if (format == AMF_SURFACE_NV12)
      have_nv12 = TRUE;
  }

  if (!have_nv12) {
    GST_WARNING_OBJECT (device, "NV12 is not supported");
    return nullptr;
  }

  num_val = in_iocaps->GetNumOfMemoryTypes ();
  GST_LOG_OBJECT (device, "Input memory type count: %d", num_val);
  for (guint i = 0; i < num_val; i++) {
    AMF_MEMORY_TYPE type;
    amf_bool native;

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
  amf_int64 _val; \
  result = amf_caps->GetProperty (prop, &_val); \
  if (result == AMF_OK) { \
    GST_INFO_OBJECT (device, G_STRINGIFY (val) ": %" G_GINT64_FORMAT, _val); \
    dev_caps.val = _val; \
  } \
} G_STMT_END

  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MAX_BITRATE, max_bitrate);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_STREAMS, num_of_streams);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MAX_PROFILE, max_profile);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MAX_TIER, max_tier);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MAX_LEVEL, max_level);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MIN_REFERENCE_FRAMES,
      min_ref_frames);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MAX_REFERENCE_FRAMES,
      max_ref_frames);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_HW_INSTANCES,
      num_of_hw_instances);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_COLOR_CONVERSION,
      color_conversion);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_PRE_ANALYSIS, pre_analysis);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_ROI, roi_map);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_HEVC_CAP_MAX_THROUGHPUT, max_throughput);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAPS_HEVC_QUERY_TIMEOUT_SUPPORT,
      query_timeout_support);
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

  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_HEVC_QP_I, default_qp_i, 26);
  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_HEVC_QP_P, default_qp_p, 26);
#undef QUERY_DEFAULT_PROP

  {
    const AMFPropertyInfo *pinfo = nullptr;
    result = comp->GetPropertyInfo (AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, &pinfo);
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

  sink_caps_str = "video/x-raw, format = (string) NV12, " + resolution_str +
      ", interlace-mode = (string) progressive";
  src_caps_str = "video/x-h265, " + resolution_str + ", profile = (string) main"
      ", stream-format = (string) byte-stream, alignment = (string) au";

  system_caps = gst_caps_from_string (sink_caps_str.c_str ());
  sink_caps = gst_caps_copy (system_caps);
  gst_caps_set_features (sink_caps, 0,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, nullptr));
  gst_caps_append (sink_caps, system_caps);

  cdata = g_new0 (GstAmfH265EncClassData, 1);
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
gst_amf_h265_enc_register_d3d11 (GstPlugin * plugin, GstD3D11Device * device,
    gpointer context, guint rank)
{
  GstAmfH265EncClassData *cdata;
  AMFContext *amf_context = (AMFContext *) context;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;

  GST_DEBUG_CATEGORY_INIT (gst_amf_h265_enc_debug, "amfh265enc", 0,
      "amfh265enc");

  result = factory->CreateComponent (amf_context, AMFVideoEncoder_HEVC, &comp);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Failed to create component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return;
  }

  cdata = gst_amf_h265_enc_create_class_data (device, comp.GetPtr ());
  if (!cdata)
    return;

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstAmfH265EncClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_amf_h265_enc_class_init,
    nullptr,
    cdata,
    sizeof (GstAmfH265Enc),
    0,
    (GInstanceInitFunc) gst_amf_h265_enc_init,
  };

  type_name = g_strdup ("GstAmfH265Enc");
  feature_name = g_strdup ("amfh265enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstAmfH265Device%dEnc", index);
    feature_name = g_strdup_printf ("amfh265device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_AMF_ENCODER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
