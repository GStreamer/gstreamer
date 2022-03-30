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

#include "gstamfh264enc.h"
#include <components/Component.h>
#include <components/VideoEncoderVCE.h>
#include <core/Factory.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/pbutils/codec-utils.h>
#include <string>
#include <set>
#include <string.h>

using namespace amf;

GST_DEBUG_CATEGORY_STATIC (gst_amf_h264_enc_debug);
#define GST_CAT_DEFAULT gst_amf_h264_enc_debug

static GTypeClass *parent_class = nullptr;

typedef struct
{
  amf_int64 max_bitrate;
  amf_int64 num_of_streams;
  amf_int64 max_profile;
  amf_int64 max_level;
  amf_int64 bframes;
  amf_int64 min_ref_frames;
  amf_int64 max_ref_frames;
  amf_int64 max_temporal_layers;
  amf_int64 fixed_slice_mode;
  amf_int64 num_of_hw_instances;
  amf_int64 color_conversion;
  amf_int64 pre_analysis;
  amf_int64 roi_map;
  amf_int64 max_throughput;
  amf_int64 query_timeout_support;
  amf_int64 default_qp_i;
  amf_int64 default_qp_p;
  amf_int64 default_qp_b;
  gboolean interlace_supported;
  guint valign;
} GstAmfH264EncDeviceCaps;

#define GST_TYPE_AMF_H264_ENC_USAGE (gst_amf_h264_enc_usage_get_type ())
static GType
gst_amf_h264_enc_usage_get_type (void)
{
  static GType usage_type = 0;
  static const GEnumValue usages[] = {
    {AMF_VIDEO_ENCODER_USAGE_TRANSCODING, "Transcoding", "transcoding"},
    {AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY, "Ultra Low Latency",
        "ultra-low-latency"},
    {AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY, "Low Latency", "low-latency"},
    {AMF_VIDEO_ENCODER_USAGE_WEBCAM, "Webcam", "webcam"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&usage_type)) {
    GType type = g_enum_register_static ("GstAmfH264EncUsage", usages);
    g_once_init_leave (&usage_type, type);
  }

  return usage_type;
}

#define GST_TYPE_AMF_H264_ENC_RATE_CONTROL (gst_amf_h264_enc_rate_control_get_type ())
static GType
gst_amf_h264_enc_rate_control_get_type (void)
{
  static GType rate_control_type = 0;
  static const GEnumValue rate_controls[] = {
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN, "Default, depends on Usage",
        "default"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP, "Constant QP", "cqp"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR, "Constant Bitrate", "cbr"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
        "Peak Constrained VBR", "vbr"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
        "Latency Constrained VBR", "lcvbr"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&rate_control_type)) {
    GType type =
        g_enum_register_static ("GstAmfH264EncRateControl", rate_controls);
    g_once_init_leave (&rate_control_type, type);
  }

  return rate_control_type;
}

#define AMF_VIDEO_ENCODER_QUALITY_PRESET_UNKNOWN -1

#define GST_TYPE_AMF_H264_ENC_PRESET (gst_amf_h264_enc_preset_get_type ())
static GType
gst_amf_h264_enc_preset_get_type (void)
{
  static GType preset_type = 0;
  static const GEnumValue presets[] = {
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_UNKNOWN, "Default, depends on USAGE",
        "default"},
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED, "Balanced", "balanced"},
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED, "Speed", "speed"},
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY, "Quality", "quality"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&preset_type)) {
    GType type = g_enum_register_static ("GstAmfH264EncPreset", presets);
    g_once_init_leave (&preset_type, type);
  }

  return preset_type;
}

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;

  gint64 adapter_luid;

  GstAmfH264EncDeviceCaps dev_caps;
} GstAmfH264EncClassData;

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
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_QP_I,
  PROP_QP_P,
  PROP_REF_FRAMES,
  PROP_AUD,
  PROP_CABAC,
};

#define DEFAULT_USAGE AMF_VIDEO_ENCODER_USAGE_TRANSCODING
#define DEFAULT_RATE_CONTROL AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN
#define DEFAULT_PRESET AMF_VIDEO_ENCODER_QUALITY_PRESET_UNKNOWN
#define DEFAULT_BITRATE 0
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_GOP_SIZE -1
#define DEFAULT_MIN_MAX_QP -1
#define DEFAULT_AUD TRUE
#define DEFAULT_CABAC TRUE

typedef struct _GstAmfH264Enc
{
  GstAmfEncoder parent;

  gboolean packetized;
  GstH264NalParser *parser;

  GMutex prop_lock;
  gboolean property_updated;

  gint usage;
  gint rate_control;
  gint preset;
  guint bitrate;
  guint max_bitrate;
  gint gop_size;
  gint min_qp;
  gint max_qp;
  guint qp_i;
  guint qp_p;
  guint ref_frames;

  gboolean aud;
  gboolean cabac;
} GstAmfH264Enc;

typedef struct _GstAmfH264EncClass
{
  GstAmfEncoderClass parent_class;
  GstAmfH264EncDeviceCaps dev_caps;

  gint64 adapter_luid;
} GstAmfH264EncClass;

#define GST_AMF_H264_ENC(object) ((GstAmfH264Enc *) (object))
#define GST_AMF_H264_ENC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstAmfH264EncClass))

static void gst_amf_h264_enc_finalize (GObject * object);
static void gst_amf_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amf_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_amf_h264_enc_getcaps (GstVideoEncoder * encoder,
    GstCaps * filter);
static gboolean gst_amf_h264_enc_set_format (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component);
static gboolean gst_amf_h264_enc_set_output_state (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component);
static gboolean gst_amf_h264_enc_set_surfrace_prop (GstAmfEncoder * encoder,
    GstVideoCodecFrame * frame, gpointer surface);
static GstBuffer *gst_amf_h264_enc_create_output_buffer (GstAmfEncoder *
    encoder, gpointer data, gboolean * sync_point);
static gboolean gst_amf_h264_enc_check_reconfigure (GstAmfEncoder * encoder);

static void
gst_amf_h264_enc_class_init (GstAmfH264EncClass * klass, gpointer data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstAmfEncoderClass *amf_class = GST_AMF_ENCODER_CLASS (klass);
  GstAmfH264EncClassData *cdata = (GstAmfH264EncClassData *) data;
  GstAmfH264EncDeviceCaps *dev_caps = &cdata->dev_caps;
  GParamFlags param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_MUTABLE_PLAYING | G_PARAM_STATIC_STRINGS);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  object_class->finalize = gst_amf_h264_enc_finalize;
  object_class->set_property = gst_amf_h264_enc_set_property;
  object_class->get_property = gst_amf_h264_enc_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of associated GPU",
          G_MININT64, G_MAXINT64, cdata->adapter_luid, param_flags));
  g_object_class_install_property (object_class, PROP_USAGE,
      g_param_spec_enum ("usage", "Usage",
          "Target usage", GST_TYPE_AMF_H264_ENC_USAGE,
          DEFAULT_USAGE, param_flags));
  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Rate Control Method", GST_TYPE_AMF_H264_ENC_RATE_CONTROL,
          DEFAULT_RATE_CONTROL, param_flags));
  g_object_class_install_property (object_class, PROP_PRESET,
      g_param_spec_enum ("preset", "Preset",
          "Preset", GST_TYPE_AMF_H264_ENC_PRESET, DEFAULT_PRESET, param_flags));
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Target bitrate in kbit/sec (0: USAGE default)",
          0, G_MAXINT / 1000, DEFAULT_BITRATE, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Maximum bitrate in kbit/sec (0: USAGE default)",
          0, G_MAXINT / 1000, DEFAULT_MAX_BITRATE, param_flags));
  g_object_class_install_property (object_class, PROP_GOP_SIZE,
      g_param_spec_int ("gop-size", "GOP Size",
          "Number of pictures within a GOP (-1: USAGE default)",
          -1, G_MAXINT, DEFAULT_GOP_SIZE, param_flags));
  g_object_class_install_property (object_class, PROP_MIN_QP,
      g_param_spec_int ("min-qp", "Min QP",
          "Minimum allowed QP value (-1: USAGE default)",
          -1, 51, DEFAULT_MIN_MAX_QP, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_QP,
      g_param_spec_int ("max-qp", "Max QP",
          "Maximum allowed QP value (-1: USAGE default)",
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
  g_object_class_install_property (object_class, PROP_CABAC,
      g_param_spec_boolean ("cabac", "CABAC",
          "Enable CABAC entropy coding", TRUE, param_flags));

  gst_element_class_set_metadata (element_class,
      "AMD AMF H.264 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode H.264 video streams using AMF API",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  videoenc_class->getcaps = GST_DEBUG_FUNCPTR (gst_amf_h264_enc_getcaps);

  amf_class->set_format = GST_DEBUG_FUNCPTR (gst_amf_h264_enc_set_format);
  amf_class->set_output_state =
      GST_DEBUG_FUNCPTR (gst_amf_h264_enc_set_output_state);
  amf_class->set_surface_prop =
      GST_DEBUG_FUNCPTR (gst_amf_h264_enc_set_surfrace_prop);
  amf_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_amf_h264_enc_create_output_buffer);
  amf_class->check_reconfigure =
      GST_DEBUG_FUNCPTR (gst_amf_h264_enc_check_reconfigure);

  klass->dev_caps = cdata->dev_caps;
  klass->adapter_luid = cdata->adapter_luid;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_amf_h264_enc_init (GstAmfH264Enc * self)
{
  GstAmfH264EncClass *klass = GST_AMF_H264_ENC_GET_CLASS (self);
  GstAmfH264EncDeviceCaps *dev_caps = &klass->dev_caps;

  gst_amf_encoder_set_subclass_data (GST_AMF_ENCODER (self),
      klass->adapter_luid, AMFVideoEncoderVCE_AVC);

  self->parser = gst_h264_nal_parser_new ();

  g_mutex_init (&self->prop_lock);

  self->usage = DEFAULT_USAGE;
  self->rate_control = DEFAULT_RATE_CONTROL;
  self->preset = DEFAULT_PRESET;
  self->bitrate = DEFAULT_BITRATE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->gop_size = DEFAULT_GOP_SIZE;
  self->min_qp = DEFAULT_MIN_MAX_QP;
  self->max_qp = DEFAULT_MIN_MAX_QP;
  self->qp_i = (guint) dev_caps->default_qp_i;
  self->qp_p = (guint) dev_caps->default_qp_p;
  self->ref_frames = (guint) dev_caps->min_ref_frames;
  self->aud = DEFAULT_AUD;
  self->cabac = DEFAULT_CABAC;
}

static void
gst_amf_h264_enc_finalize (GObject * object)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (object);

  gst_h264_nal_parser_free (self->parser);
  g_mutex_clear (&self->prop_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_int (GstAmfH264Enc * self, gint * old_val, const GValue * new_val)
{
  gint val = g_value_get_int (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_uint (GstAmfH264Enc * self, guint * old_val, const GValue * new_val)
{
  guint val = g_value_get_uint (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_enum (GstAmfH264Enc * self, gint * old_val, const GValue * new_val)
{
  gint val = g_value_get_enum (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
update_bool (GstAmfH264Enc * self, gboolean * old_val, const GValue * new_val)
{
  gboolean val = g_value_get_boolean (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  self->property_updated = TRUE;
}

static void
gst_amf_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (object);

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
      update_int (self, &self->gop_size, value);
      break;
    case PROP_MIN_QP:
      update_int (self, &self->min_qp, value);
      break;
    case PROP_MAX_QP:
      update_int (self, &self->max_qp, value);
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
    case PROP_CABAC:
      update_bool (self, &self->cabac, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&self->prop_lock);
}

static void
gst_amf_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmfH264EncClass *klass = GST_AMF_H264_ENC_GET_CLASS (object);
  GstAmfH264Enc *self = GST_AMF_H264_ENC (object);

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
      g_value_set_int (value, self->gop_size);
      break;
    case PROP_MIN_QP:
      g_value_set_int (value, self->min_qp);
      break;
    case PROP_MAX_QP:
      g_value_set_int (value, self->max_qp);
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
    case PROP_CABAC:
      g_value_set_boolean (value, self->cabac);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amf_h264_enc_get_downstream_profiles_and_format (GstAmfH264Enc * self,
    std::set < std::string > &downstream_profiles, gboolean * packetized)
{
  GstCaps *allowed_caps;
  GstStructure *s;
  const gchar *stream_format;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (self));

  if (!allowed_caps || gst_caps_is_empty (allowed_caps) ||
      gst_caps_is_any (allowed_caps)) {
    gst_clear_caps (&allowed_caps);

    return;
  }

  for (guint i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    const GValue *profile_value;
    const gchar *profile;

    s = gst_caps_get_structure (allowed_caps, i);
    profile_value = gst_structure_get_value (s, "profile");
    if (!profile_value)
      continue;

    if (GST_VALUE_HOLDS_LIST (profile_value)) {
      for (guint j = 0; j < gst_value_list_get_size (profile_value); j++) {
        const GValue *p = gst_value_list_get_value (profile_value, j);

        if (!G_VALUE_HOLDS_STRING (p))
          continue;

        profile = g_value_get_string (p);
        if (profile)
          downstream_profiles.insert (profile);
      }

    } else if (G_VALUE_HOLDS_STRING (profile_value)) {
      profile = g_value_get_string (profile_value);
      if (profile)
        downstream_profiles.insert (profile);
    }
  }

  if (packetized) {
    *packetized = FALSE;
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);
    stream_format = gst_structure_get_string (s, "stream-format");
    if (g_strcmp0 (stream_format, "avc") == 0)
      *packetized = TRUE;
  }

  gst_caps_unref (allowed_caps);
}

static GstCaps *
gst_amf_h264_enc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (encoder);
  GstAmfH264EncClass *klass = GST_AMF_H264_ENC_GET_CLASS (self);
  GstCaps *template_caps;
  GstCaps *supported_caps;
  std::set < std::string > downstream_profiles;

  if (!klass->dev_caps.interlace_supported)
    return gst_video_encoder_proxy_getcaps (encoder, nullptr, filter);

  gst_amf_h264_enc_get_downstream_profiles_and_format (self,
      downstream_profiles, nullptr);

  GST_DEBUG_OBJECT (self, "Downstream specified %" G_GSIZE_FORMAT " profiles",
      downstream_profiles.size ());

  if (downstream_profiles.size () == 0)
    return gst_video_encoder_proxy_getcaps (encoder, NULL, filter);

  /* Profile allows interlaced? */
  /* *INDENT-OFF* */
  gboolean can_support_interlaced = FALSE;
  for (const auto &iter: downstream_profiles) {
    if (iter == "high" || iter == "main" || iter == "constrained-high") {
      can_support_interlaced = TRUE;
      break;
    }
  }
  /* *INDENT-ON* */

  GST_DEBUG_OBJECT (self, "Downstream %s support interlaced format",
      can_support_interlaced ? "can" : "cannot");

  if (can_support_interlaced) {
    /* No special handling is needed */
    return gst_video_encoder_proxy_getcaps (encoder, nullptr, filter);
  }

  template_caps = gst_pad_get_pad_template_caps (encoder->sinkpad);
  template_caps = gst_caps_make_writable (template_caps);

  gst_caps_set_simple (template_caps, "interlace-mode", G_TYPE_STRING,
      "progressive", nullptr);

  supported_caps = gst_video_encoder_proxy_getcaps (encoder,
      template_caps, filter);
  gst_caps_unref (template_caps);

  GST_DEBUG_OBJECT (self, "Returning %" GST_PTR_FORMAT, supported_caps);

  return supported_caps;
}

static gboolean
gst_amf_h264_enc_set_format (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (encoder);
  GstAmfH264EncClass *klass = GST_AMF_H264_ENC_GET_CLASS (self);
  GstAmfH264EncDeviceCaps *dev_caps = &klass->dev_caps;
  AMFComponent *comp = (AMFComponent *) component;
  GstVideoInfo *info = &state->info;
  std::set < std::string > downstream_profiles;
  AMF_VIDEO_ENCODER_PROFILE_ENUM profile = AMF_VIDEO_ENCODER_PROFILE_UNKNOWN;
  AMF_RESULT result;
  AMFRate framerate;
  AMFRatio aspect_ratio;
  amf_int64 int64_val;
  amf_bool boolean_val;
  AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM rc_mode;
  AMF_VIDEO_ENCODER_CODING_ENUM cabac = AMF_VIDEO_ENCODER_UNDEFINED;

  self->packetized = FALSE;

  gst_amf_h264_enc_get_downstream_profiles_and_format (self,
      downstream_profiles, &self->packetized);

  if (downstream_profiles.empty ()) {
    GST_ERROR_OBJECT (self, "Unable to get downstream profile");
    return FALSE;
  }

  if (GST_VIDEO_INFO_IS_INTERLACED (info)) {
    downstream_profiles.erase ("constrained-high");
    downstream_profiles.erase ("constrained-baseline");
    downstream_profiles.erase ("baseline");

    if (downstream_profiles.empty ()) {
      GST_ERROR_OBJECT (self,
          "None of downstream profile supports interlaced encoding");
      return FALSE;
    }
  }

  if (downstream_profiles.find ("main") != downstream_profiles.end ()) {
    profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
  } else if (downstream_profiles.find ("high") != downstream_profiles.end ()) {
    profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
  } else if (downstream_profiles.find ("constrained-high") !=
      downstream_profiles.end ()) {
    if (dev_caps->max_profile >=
        (gint64) AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH) {
      profile = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH;
    } else {
      profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
    }
  } else if (downstream_profiles.find ("constrained-baseline") !=
      downstream_profiles.end ()) {
    if (dev_caps->max_profile >=
        (gint64) AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE) {
      profile = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE;
    } else {
      profile = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
    }
  } else if (downstream_profiles.find ("baseline") !=
      downstream_profiles.end ()) {
    profile = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
  } else {
    GST_ERROR_OBJECT (self, "Failed to determine profile");
    return FALSE;
  }

  g_mutex_lock (&self->prop_lock);
  /* Configure static properties first before Init() */
  result = comp->SetProperty (AMF_VIDEO_ENCODER_FRAMESIZE,
      AMFConstructSize (info->width, info->height));
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set frame size, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_USAGE, (amf_int64) self->usage);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set usage, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_PROFILE, (amf_int64) profile);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set profile, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES,
      (amf_int64) self->ref_frames);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set ref-frames, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  aspect_ratio = AMFConstructRatio (info->par_n, info->par_d);
  result = comp->SetProperty (AMF_VIDEO_ENCODER_ASPECT_RATIO, aspect_ratio);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set aspect ratio, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (info->colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255)
    boolean_val = true;
  else
    boolean_val = false;

  result = comp->SetProperty (AMF_VIDEO_ENCODER_FULL_RANGE_COLOR, boolean_val);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set full-range-color, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (self->rate_control != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
        (amf_int64) self->rate_control);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set rate-control, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  result = comp->Init (AMF_SURFACE_NV12, info->width, info->height);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to init component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  /* dynamic properties */
  result = comp->GetProperty (AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
      &int64_val);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to get rate-control method, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  rc_mode = (AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM) int64_val;
  if (self->min_qp >= 0)
    comp->SetProperty (AMF_VIDEO_ENCODER_MIN_QP, (amf_int64) self->min_qp);
  if (self->max_qp >= 0)
    comp->SetProperty (AMF_VIDEO_ENCODER_MAX_QP, (amf_int64) self->max_qp);

  comp->SetProperty (AMF_VIDEO_ENCODER_QP_I, (amf_int64) self->qp_i);
  comp->SetProperty (AMF_VIDEO_ENCODER_QP_P, (amf_int64) self->qp_p);

  switch (rc_mode) {
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR:
      if (self->bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_TARGET_BITRATE,
            (amf_int64) self->bitrate * 1000);
        comp->SetProperty (AMF_VIDEO_ENCODER_PEAK_BITRATE,
            (amf_int64) self->bitrate * 1000);
      }
      break;
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR:
    case AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR:
      if (self->bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_TARGET_BITRATE,
            (amf_int64) self->bitrate * 1000);
      }
      if (self->max_bitrate > 0) {
        comp->SetProperty (AMF_VIDEO_ENCODER_PEAK_BITRATE,
            (amf_int64) self->max_bitrate * 1000);
      }
      break;
    default:
      break;
  }

  /* Disable frame skip for now, need investigation the behavior */
  result = comp->SetProperty (AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE,
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

  result = comp->SetProperty (AMF_VIDEO_ENCODER_FRAMERATE, framerate);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set frame rate, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  if (self->gop_size >= 0) {
    result = comp->SetProperty (AMF_VIDEO_ENCODER_IDR_PERIOD,
        (amf_int64) self->gop_size);
    if (result != AMF_OK) {
      GST_ERROR_OBJECT (self, "Failed to set IDR period, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
      goto error;
    }
  }

  if (profile != AMF_VIDEO_ENCODER_PROFILE_BASELINE &&
      profile != AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE) {
    if (self->cabac)
      cabac = AMF_VIDEO_ENCODER_CABAC;
    else
      cabac = AMF_VIDEO_ENCODER_CALV;
  }

  result = comp->SetProperty (AMF_VIDEO_ENCODER_CABAC_ENABLE,
      (amf_int64) cabac);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to set cabac, result %"
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
gst_amf_h264_enc_set_output_state (GstAmfEncoder * encoder,
    GstVideoCodecState * state, gpointer component)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (encoder);
  AMFComponent *comp = (AMFComponent *) component;
  GstVideoCodecState *output_state;
  GstCaps *caps;
  const gchar *profile_from_sps;
  std::set < std::string > downstream_profiles;
  std::string caps_str;
  GstTagList *tags;
  GstBuffer *codec_data = nullptr;
  GstH264NalUnit sps_nalu, pps_nalu;
  GstH264ParserResult rst;
  AMF_RESULT result;
  AMFInterfacePtr iface;
  AMFBufferPtr spspps_buf;
  guint8 *spspps;
  amf_size spspps_size;

  result = comp->GetProperty (AMF_VIDEO_ENCODER_EXTRADATA, &iface);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to get extra data, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return FALSE;
  }

  spspps_buf = AMFBufferPtr (iface);
  if (!spspps_buf) {
    GST_ERROR_OBJECT (self, "Failed to set get AMFBuffer interface, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return FALSE;
  }

  spspps_size = spspps_buf->GetSize ();
  if (spspps_size < 4) {
    GST_ERROR_OBJECT (self, "Too small spspps size %d", (guint) spspps_size);
    return FALSE;
  }

  spspps = (guint8 *) spspps_buf->GetNative ();
  if (!spspps) {
    GST_ERROR_OBJECT (self, "Null SPS/PPS");
    return FALSE;
  }

  caps_str = "video/x-h264, alignment = (string) au";
  gst_amf_h264_enc_get_downstream_profiles_and_format (self,
      downstream_profiles, nullptr);

  rst = gst_h264_parser_identify_nalu (self->parser,
      spspps, 0, spspps_size, &sps_nalu);
  if (rst != GST_H264_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to identify SPS nal");
    return FALSE;
  }

  if (sps_nalu.size < 4) {
    GST_ERROR_OBJECT (self, "Too small sps nal size %d", sps_nalu.size);
    return FALSE;
  }

  rst = gst_h264_parser_identify_nalu_unchecked (self->parser,
      spspps, sps_nalu.offset + sps_nalu.size, spspps_size, &pps_nalu);
  if (rst != GST_H264_PARSER_OK && self->packetized) {
    GST_ERROR_OBJECT (self, "Failed to identify PPS nal, %d", rst);
    return FALSE;
  }

  if (self->packetized) {
    GstMapInfo info;
    guint8 *data;
    guint8 profile_idc, profile_comp, level_idc;
    const guint nal_length_size = 4;
    const guint num_sps = 1;
    const guint num_pps = 1;

    data = sps_nalu.data + sps_nalu.offset + sps_nalu.header_bytes;
    profile_idc = data[0];
    profile_comp = data[1];
    level_idc = data[2];

    /* 5: configuration version, profile, compatibility, level, nal length
     * 1: num sps
     * 2: sps size bytes
     * sizeof (sps)
     * 1: num pps
     * 2: pps size bytes
     * sizeof (pps)
     *
     * -> 11 + sps_size + pps_size
     */
    codec_data = gst_buffer_new_and_alloc (11 + sps_nalu.size + pps_nalu.size);

    gst_buffer_map (codec_data, &info, GST_MAP_WRITE);

    data = (guint8 *) info.data;
    data[0] = 1;
    data[1] = profile_idc;
    data[2] = profile_comp;
    data[3] = level_idc;
    data[4] = 0xfc | (nal_length_size - 1);
    data[5] = 0xe0 | num_sps;
    data += 6;
    GST_WRITE_UINT16_BE (data, sps_nalu.size);
    data += 2;
    memcpy (data, sps_nalu.data + sps_nalu.offset, sps_nalu.size);
    data += sps_nalu.size;

    data[0] = num_pps;
    data++;

    GST_WRITE_UINT16_BE (data, pps_nalu.size);
    data += 2;
    memcpy (data, pps_nalu.data + pps_nalu.offset, pps_nalu.size);

    gst_buffer_unmap (codec_data, &info);
  }

  profile_from_sps =
      gst_codec_utils_h264_get_profile (sps_nalu.data + sps_nalu.offset +
      sps_nalu.header_bytes, 3);

  if (!profile_from_sps) {
    GST_WARNING_OBJECT (self, "Failed to parse profile from SPS");
  } else if (!downstream_profiles.empty ()) {
    if (downstream_profiles.find (profile_from_sps) !=
        downstream_profiles.end ()) {
      caps_str += ", profile = (string) " + std::string (profile_from_sps);
    } else if (downstream_profiles.find ("baseline") !=
        downstream_profiles.end () &&
        strcmp (profile_from_sps, "constrained-baseline") == 0) {
      caps_str += ", profile = (string) baseline";
    } else if (downstream_profiles.find ("constrained-baseline") !=
        downstream_profiles.end () &&
        strcmp (profile_from_sps, "constrained-baseline") == 0) {
      caps_str += ", profile = (string) constrained-baseline";
    }
  } else {
    caps_str += ", profile = (string) " + std::string (profile_from_sps);
  }

  if (self->packetized) {
    caps_str += ", stream-format = (string) avc";
  } else {
    caps_str += ", stream-format = (string) byte-stream";
  }

  caps = gst_caps_from_string (caps_str.c_str ());

  if (self->packetized) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data,
        nullptr);
    gst_buffer_unref (codec_data);
  }

  output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      caps, state);

  GST_INFO_OBJECT (self, "Output caps: %" GST_PTR_FORMAT, output_state->caps);
  gst_video_codec_state_unref (output_state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      "amfh264enc", nullptr);

  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder),
      tags, GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static gboolean
gst_amf_h264_enc_set_surfrace_prop (GstAmfEncoder * encoder,
    GstVideoCodecFrame * frame, gpointer surface)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (encoder);
  AMFSurface *surf = (AMFSurface *) surface;
  AMF_RESULT result;
  amf_bool insert_aud = self->aud ? true : false;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    amf_int64 type = (amf_int64) AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR;
    result = surf->SetProperty (AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, type);
    if (result != AMF_OK) {
      GST_WARNING_OBJECT (encoder, "Failed to set force idr, result %"
          GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    }
  }

  result = surf->SetProperty (AMF_VIDEO_ENCODER_INSERT_AUD, &insert_aud);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (encoder, "Failed to set AUD, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
  }

  return TRUE;
}

static GstBuffer *
gst_amf_h264_enc_create_output_buffer (GstAmfEncoder * encoder,
    gpointer data, gboolean * sync_point)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (encoder);
  AMFBuffer *amf_buf = (AMFBuffer *) data;
  GstBuffer *buf;
  GstH264ParserResult rst;
  GstH264NalUnit nalu;
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

  if (!self->packetized) {
    buf = gst_buffer_new_memdup (data_ptr, data_size);
  } else {
    buf = gst_buffer_new ();
    rst = gst_h264_parser_identify_nalu (self->parser,
        data_ptr, 0, data_size, &nalu);
    if (rst == GST_H264_PARSER_NO_NAL_END)
      rst = GST_H264_PARSER_OK;

    while (rst == GST_H264_PARSER_OK) {
      GstMemory *mem;
      guint8 *data;

      data = (guint8 *) g_malloc0 (nalu.size + 4);
      GST_WRITE_UINT32_BE (data, nalu.size);
      memcpy (data + 4, nalu.data + nalu.offset, nalu.size);

      mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, data, nalu.size + 4,
          0, nalu.size + 4, data, (GDestroyNotify) g_free);
      gst_buffer_append_memory (buf, mem);

      rst = gst_h264_parser_identify_nalu (self->parser,
          data_ptr, nalu.offset + nalu.size, data_size, &nalu);

      if (rst == GST_H264_PARSER_NO_NAL_END)
        rst = GST_H264_PARSER_OK;
    }
  }

  result = amf_buf->GetProperty (AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE,
      &output_type);
  if (result == AMF_OK &&
      output_type == (amf_int64) AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR) {
    *sync_point = TRUE;
  }

  return buf;
}

static gboolean
gst_amf_h264_enc_check_reconfigure (GstAmfEncoder * encoder)
{
  GstAmfH264Enc *self = GST_AMF_H264_ENC (encoder);
  gboolean ret;

  g_mutex_lock (&self->prop_lock);
  ret = self->property_updated;
  g_mutex_unlock (&self->prop_lock);

  return ret;
}

static GstAmfH264EncClassData *
gst_amf_h264_enc_create_class_data (GstD3D11Device * device,
    AMFComponent * comp)
{
  AMF_RESULT result;
  GstAmfH264EncDeviceCaps dev_caps = { 0, };
  std::string sink_caps_str;
  std::string src_caps_str;
  std::set < std::string > profiles;
  std::string profile_str;
  std::string resolution_str;
  GstAmfH264EncClassData *cdata;
  AMFCapsPtr amf_caps;
  AMFIOCapsPtr in_iocaps;
  AMFIOCapsPtr out_iocaps;
  amf_int32 in_min_width = 0, in_max_width = 0;
  amf_int32 in_min_height = 0, in_max_height = 0;
  amf_int32 out_min_width = 0, out_max_width = 0;
  amf_int32 out_min_height = 0, out_max_height = 0;
  amf_bool interlace_supported;
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
  interlace_supported = in_iocaps->IsInterlacedSupported ();

  GST_INFO_OBJECT (device, "Input width: [%d, %d], height: [%d, %d], "
      "valign: %d, interlace supported: %d",
      in_min_width, in_max_width, in_min_height, in_max_height, dev_caps.valign,
      interlace_supported);

  if (interlace_supported)
    dev_caps.interlace_supported = TRUE;

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

  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MAX_BITRATE, max_bitrate);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_NUM_OF_STREAMS, num_of_streams);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MAX_PROFILE, max_profile);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MAX_LEVEL, max_level);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_BFRAMES, bframes);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MIN_REFERENCE_FRAMES, min_ref_frames);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MAX_REFERENCE_FRAMES, max_ref_frames);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MAX_TEMPORAL_LAYERS,
      max_temporal_layers);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_FIXED_SLICE_MODE, fixed_slice_mode);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES,
      num_of_hw_instances);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_COLOR_CONVERSION, color_conversion);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_PRE_ANALYSIS, pre_analysis);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_ROI, roi_map);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAP_MAX_THROUGHPUT, max_throughput);
  QUERY_CAPS_PROP (AMF_VIDEO_ENCODER_CAPS_QUERY_TIMEOUT_SUPPORT,
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

  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_QP_I, default_qp_i, 22);
  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_QP_I, default_qp_p, 22);
  QUERY_DEFAULT_PROP (AMF_VIDEO_ENCODER_QP_I, default_qp_b, 22);
#undef QUERY_DEFAULT_PROP

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

  if (dev_caps.max_profile >= (gint64) AMF_VIDEO_ENCODER_PROFILE_BASELINE) {
    profiles.insert ("baseline");
    profiles.insert ("constrained-baseline");
  }

  if (dev_caps.max_profile >= (gint64) AMF_VIDEO_ENCODER_PROFILE_MAIN)
    profiles.insert ("main");

  if (dev_caps.max_profile >= (gint64) AMF_VIDEO_ENCODER_PROFILE_HIGH) {
    profiles.insert ("high");
  }

  if (dev_caps.max_profile >=
      (gint64) AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH) {
    profiles.insert ("constrained-high");
  }

  if (profiles.empty ()) {
    GST_WARNING_OBJECT (device, "Failed to determine profile support");
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

  if (profiles.size () == 1) {
    profile_str = "profile = (string) " + *(profiles.begin ());
  } else {
    gboolean first = TRUE;

    profile_str = "profile = (string) { ";
    APPEND_STRING (profile_str, profiles, "main");
    APPEND_STRING (profile_str, profiles, "high");
    APPEND_STRING (profile_str, profiles, "constrained-high");
    APPEND_STRING (profile_str, profiles, "constrained-baseline");
    APPEND_STRING (profile_str, profiles, "baseline");
    profile_str += " } ";
  }
#undef APPEND_STRING

  resolution_str = "width = (int) [ " + std::to_string (min_width)
      + ", " + std::to_string (max_width) + " ]";
  resolution_str += ", height = (int) [ " + std::to_string (min_height)
      + ", " + std::to_string (max_height) + " ]";

  sink_caps_str = "video/x-raw, format = (string) NV12, " + resolution_str;
  if (dev_caps.interlace_supported > 0) {
    sink_caps_str += ", interlace-mode = (string) { interleaved, mixed }";
  } else {
    sink_caps_str += ", interlace-mode = (string) progressive";
  }

  src_caps_str = "video/x-h264, " + resolution_str + ", " + profile_str +
      ", stream-format = (string) { avc, byte-stream }, alignment = (string) au";

  system_caps = gst_caps_from_string (sink_caps_str.c_str ());
  sink_caps = gst_caps_copy (system_caps);
  gst_caps_set_features (sink_caps, 0,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, nullptr));
  gst_caps_append (sink_caps, system_caps);

  cdata = g_new0 (GstAmfH264EncClassData, 1);
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
gst_amf_h264_enc_register_d3d11 (GstPlugin * plugin, GstD3D11Device * device,
    gpointer context, guint rank)
{
  GstAmfH264EncClassData *cdata;
  AMFContext *amf_context = (AMFContext *) context;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;

  GST_DEBUG_CATEGORY_INIT (gst_amf_h264_enc_debug, "amfh264enc", 0,
      "amfh264enc");

  result = factory->CreateComponent (amf_context, AMFVideoEncoderVCE_AVC,
      &comp);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Failed to create component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return;
  }

  cdata = gst_amf_h264_enc_create_class_data (device, comp.GetPtr ());
  if (!cdata)
    return;

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstAmfH264EncClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_amf_h264_enc_class_init,
    nullptr,
    cdata,
    sizeof (GstAmfH264Enc),
    0,
    (GInstanceInitFunc) gst_amf_h264_enc_init,
  };

  type_name = g_strdup ("GstAmfH264Enc");
  feature_name = g_strdup ("amfh264enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstAmfH264Device%dEnc", index);
    feature_name = g_strdup_printf ("amfh264device%denc", index);
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
