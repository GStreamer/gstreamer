/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-mfvp9enc
 * @title: mfvp9enc
 *
 * This element encodes raw video into VP9 compressed data.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v videotestsrc ! mfvp9enc ! matroskamux ! filesink location=videotestsrc.mkv
 * ]| This example pipeline will encode a test video source to VP9 using
 * Media Foundation encoder, and muxes it in a mkv container.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstmfvideoenc.h"
#include "gstmfvp9enc.h"
#include <wrl.h>

using namespace Microsoft::WRL;

GST_DEBUG_CATEGORY (gst_mf_vp9_enc_debug);
#define GST_CAT_DEFAULT gst_mf_vp9_enc_debug

enum
{
  GST_MF_VP9_ENC_RC_MODE_CBR = 0,
  GST_MF_VP9_ENC_RC_MODE_QUALITY,
};

#define GST_TYPE_MF_VP9_ENC_RC_MODE (gst_mf_vp9_enc_rc_mode_get_type())
static GType
gst_mf_vp9_enc_rc_mode_get_type (void)
{
  static GType rc_mode_type = 0;

  static const GEnumValue rc_mode_types[] = {
    {GST_MF_VP9_ENC_RC_MODE_CBR, "Constant bitrate", "cbr"},
    {GST_MF_VP9_ENC_RC_MODE_QUALITY, "Quality-based variable bitrate", "qvbr"},
    {0, NULL, NULL}
  };

  if (!rc_mode_type) {
    rc_mode_type = g_enum_register_static ("GstMFVP9EncRCMode", rc_mode_types);
  }
  return rc_mode_type;
}

enum
{
  GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN,
  GST_MF_VP9_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
};

#define GST_TYPE_MF_VP9_ENC_CONTENT_TYPE (gst_mf_vp9_enc_content_type_get_type())
static GType
gst_mf_vp9_enc_content_type_get_type (void)
{
  static GType content_type = 0;

  static const GEnumValue content_types[] = {
    {GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN, "Unknown", "unknown"},
    {GST_MF_VP9_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
        "Fixed Camera Angle, such as a webcam", "fixed"},
    {0, NULL, NULL}
  };

  if (!content_type) {
    content_type =
        g_enum_register_static ("GstMFVP9EncContentType", content_types);
  }
  return content_type;
}

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_RC_MODE,
  PROP_MAX_BITRATE,
  PROP_QUALITY_VS_SPEED,
  PROP_GOP_SIZE,
  PROP_THREADS,
  PROP_CONTENT_TYPE,
  PROP_LOW_LATENCY,
};

#define DEFAULT_BITRATE (2 * 1024)
#define DEFAULT_RC_MODE GST_MF_VP9_ENC_RC_MODE_CBR
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_QUALITY_VS_SPEED 50
#define DEFAULT_GOP_SIZE 0
#define DEFAULT_THREADS 0
#define DEFAULT_CONTENT_TYPE GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN
#define DEFAULT_LOW_LATENCY FALSE

#define GST_MF_VP9_ENC_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE (obj), GstMFVP9EncClass))

typedef struct _GstMFVP9EncDeviceCaps
{
  gboolean rc_mode;           /* AVEncCommonRateControlMode */
  gboolean max_bitrate;       /* AVEncCommonMaxBitRate */
  gboolean quality_vs_speed;  /* AVEncCommonQualityVsSpeed */
  gboolean gop_size;          /* AVEncMPVGOPSize */
  gboolean threads;           /* AVEncNumWorkerThreads */
  gboolean content_type;      /* AVEncVideoContentType */
  gboolean force_keyframe;    /* AVEncVideoForceKeyFrame */
  gboolean low_latency;       /* AVLowLatencyMode */
} GstMFVP9EncDeviceCaps;

typedef struct _GstMFVP9Enc
{
  GstMFVideoEnc parent;

  /* properteies */
  guint bitrate;

  /* device dependent properties */
  guint rc_mode;
  guint max_bitrate;
  guint quality_vs_speed;
  guint gop_size;
  guint threads;
  guint content_type;
  gboolean low_latency;
} GstMFVP9Enc;

typedef struct _GstMFVP9EncClass
{
  GstMFVideoEncClass parent_class;

  GstMFVP9EncDeviceCaps device_caps;
} GstMFVP9EncClass;

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gchar *device_name;
  guint32 enum_flags;
  guint device_index;
  GstMFVP9EncDeviceCaps device_caps;
  gboolean is_default;
} GstMFVP9EncClassData;

static GstElementClass *parent_class = NULL;

static void gst_mf_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mf_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_mf_vp9_enc_set_option (GstMFVideoEnc * mfenc,
    IMFMediaType * output_type);
static gboolean gst_mf_vp9_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type);

static void
gst_mf_vp9_enc_class_init (GstMFVP9EncClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstMFVideoEncClass *mfenc_class = GST_MF_VIDEO_ENC_CLASS (klass);
  GstMFVP9EncClassData *cdata = (GstMFVP9EncClassData *) data;
  GstMFVP9EncDeviceCaps *device_caps = &cdata->device_caps;
  gchar *long_name;
  gchar *classification;

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  klass->device_caps = *device_caps;

  gobject_class->get_property = gst_mf_vp9_enc_get_property;
  gobject_class->set_property = gst_mf_vp9_enc_set_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          (G_MAXUINT >> 10), DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  if (device_caps->rc_mode) {
    g_object_class_install_property (gobject_class, PROP_RC_MODE,
        g_param_spec_enum ("rc-mode", "Rate Control Mode",
            "Rate Control Mode",
            GST_TYPE_MF_VP9_ENC_RC_MODE, DEFAULT_RC_MODE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_VP9_ENC_RC_MODE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->max_bitrate) {
    g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
        g_param_spec_uint ("max-bitrate", "Max Bitrate",
            "The maximum bitrate applied when rc-mode is \"pcvbr\" in kbit/sec "
            "(0 = MFT default)", 0, (G_MAXUINT >> 10),
            DEFAULT_MAX_BITRATE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->quality_vs_speed) {
    g_object_class_install_property (gobject_class, PROP_QUALITY_VS_SPEED,
        g_param_spec_uint ("quality-vs-speed", "Quality Vs Speed",
            "Quality and speed tradeoff, [0, 33]: Low complexity, "
            "[34, 66]: Medium complexity, [67, 100]: High complexity", 0, 100,
            DEFAULT_QUALITY_VS_SPEED,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->gop_size) {
    g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
        g_param_spec_uint ("gop-size", "GOP size",
            "The number of pictures from one GOP header to the next, "
            "(0 = MFT default)", 0, G_MAXUINT - 1,
            DEFAULT_GOP_SIZE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->threads) {
    g_object_class_install_property (gobject_class, PROP_THREADS,
        g_param_spec_uint ("threads", "Threads",
            "The number of worker threads used by a encoder, "
            "(0 = MFT default)", 0, 16,
            DEFAULT_THREADS,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->content_type) {
    g_object_class_install_property (gobject_class, PROP_CONTENT_TYPE,
        g_param_spec_enum ("content-type", "Content Type",
            "Indicates the type of video content",
            GST_TYPE_MF_VP9_ENC_CONTENT_TYPE, DEFAULT_CONTENT_TYPE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_VP9_ENC_CONTENT_TYPE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->low_latency) {
    g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
        g_param_spec_boolean ("low-latency", "Low Latency",
            "Enable low latency encoding",
            DEFAULT_LOW_LATENCY,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  long_name = g_strdup_printf ("Media Foundation %s", cdata->device_name);
  classification = g_strdup_printf ("Codec/Encoder/Video%s",
      (cdata->enum_flags & MFT_ENUM_FLAG_HARDWARE) == MFT_ENUM_FLAG_HARDWARE ?
          "/Hardware" : "");
  gst_element_class_set_metadata (element_class, long_name,
      classification,
      "Microsoft Media Foundation VP9 Encoder",
      "Seungha Yang <seungha@centricular.com>");
  g_free (long_name);
  g_free (classification);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  mfenc_class->set_option = GST_DEBUG_FUNCPTR (gst_mf_vp9_enc_set_option);
  mfenc_class->set_src_caps = GST_DEBUG_FUNCPTR (gst_mf_vp9_enc_set_src_caps);

  mfenc_class->codec_id = MFVideoFormat_VP90;
  mfenc_class->enum_flags = cdata->enum_flags;
  mfenc_class->device_index = cdata->device_index;
  mfenc_class->can_force_keyframe = device_caps->force_keyframe;

  g_free (cdata->device_name);
  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_mf_vp9_enc_init (GstMFVP9Enc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->rc_mode = DEFAULT_RC_MODE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->quality_vs_speed = DEFAULT_QUALITY_VS_SPEED;
  self->gop_size = DEFAULT_GOP_SIZE;
  self->threads = DEFAULT_THREADS;
  self->content_type = DEFAULT_CONTENT_TYPE;
  self->low_latency = DEFAULT_LOW_LATENCY;
}

static void
gst_mf_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_RC_MODE:
      g_value_set_enum (value, self->rc_mode);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, self->max_bitrate);
      break;
    case PROP_QUALITY_VS_SPEED:
      g_value_set_uint (value, self->quality_vs_speed);
      break;
    case PROP_GOP_SIZE:
      g_value_set_uint (value, self->gop_size);
      break;
    case PROP_THREADS:
      g_value_set_uint (value, self->threads);
      break;
    case PROP_CONTENT_TYPE:
      g_value_set_enum (value, self->content_type);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, self->low_latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case PROP_RC_MODE:
      self->rc_mode = g_value_get_enum (value);
      break;
    case PROP_MAX_BITRATE:
      self->max_bitrate = g_value_get_uint (value);
      break;
    case PROP_QUALITY_VS_SPEED:
      self->quality_vs_speed = g_value_get_uint (value);
      break;
    case PROP_GOP_SIZE:
      self->gop_size = g_value_get_uint (value);
      break;
    case PROP_THREADS:
      self->threads = g_value_get_uint (value);
      break;
    case PROP_CONTENT_TYPE:
      self->content_type = g_value_get_enum (value);
      break;
    case PROP_LOW_LATENCY:
      self->low_latency = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint
gst_mf_vp9_enc_rc_mode_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_VP9_ENC_RC_MODE_CBR:
      return eAVEncCommonRateControlMode_CBR;
    case GST_MF_VP9_ENC_RC_MODE_QUALITY:
      return eAVEncCommonRateControlMode_Quality;
    default:
      return G_MAXUINT;
  }
}

static guint
gst_mf_vp9_enc_content_type_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN:
      return eAVEncVideoContentType_Unknown;
    case GST_MF_VP9_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE:
      return eAVEncVideoContentType_FixedCameraAngle;
    default:
      return G_MAXUINT;
  }
}

#define WARNING_HR(hr,func) \
  G_STMT_START { \
    if (!gst_mf_result (hr)) { \
      GST_WARNING_OBJECT (self, G_STRINGIFY(func) " failed, hr: 0x%x", (guint) hr); \
    } \
  } G_STMT_END

static gboolean
gst_mf_vp9_enc_set_option (GstMFVideoEnc * mfenc, IMFMediaType * output_type)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) mfenc;
  GstMFVP9EncClass *klass = GST_MF_VP9_ENC_GET_CLASS (self);
  GstMFVP9EncDeviceCaps *device_caps = &klass->device_caps;
  HRESULT hr;
  GstMFTransform *transform = mfenc->transform;

  hr = output_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_VP90);
  if (!gst_mf_result (hr))
    return FALSE;

  if (!gst_mf_result (hr))
    return FALSE;

  hr = output_type->SetUINT32 (MF_MT_AVG_BITRATE,
      MIN (self->bitrate * 1024, G_MAXUINT - 1));
  if (!gst_mf_result (hr))
    return FALSE;

  if (device_caps->rc_mode) {
    guint rc_mode;
    rc_mode = gst_mf_vp9_enc_rc_mode_to_enum (self->rc_mode);
    if (rc_mode != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncCommonRateControlMode, rc_mode);
      WARNING_HR (hr, CODECAPI_AVEncCommonRateControlMode);
    }
  }

  if (device_caps->max_bitrate && self->max_bitrate > 0) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonMaxBitRate,
        MIN (self->max_bitrate * 1024, G_MAXUINT - 1));
    WARNING_HR (hr, CODECAPI_AVEncCommonMaxBitRate);
  }

  if (device_caps->quality_vs_speed) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonQualityVsSpeed,
        self->quality_vs_speed);
    WARNING_HR (hr, CODECAPI_AVEncCommonQualityVsSpeed);
  }

  if (device_caps->gop_size) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncMPVGOPSize, self->gop_size);
    WARNING_HR (hr, CODECAPI_AVEncMPVGOPSize);
  }

  if (device_caps->threads) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncNumWorkerThreads, self->threads);
    WARNING_HR (hr, CODECAPI_AVEncNumWorkerThreads);
  }

  if (device_caps->content_type) {
    guint content_type;
    content_type = gst_mf_vp9_enc_content_type_to_enum (self->content_type);
    if (content_type != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncVideoContentType, content_type);
      WARNING_HR (hr, CODECAPI_AVEncVideoContentType);
    }
  }

  if (device_caps->low_latency) {
    hr = gst_mf_transform_set_codec_api_boolean (transform,
        &CODECAPI_AVLowLatencyMode, self->low_latency);
    WARNING_HR (hr, CODECAPI_AVLowLatencyMode);
  }

  return TRUE;
}

static gboolean
gst_mf_vp9_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) mfenc;
  GstVideoCodecState *out_state;
  GstStructure *s;
  GstCaps *out_caps;
  GstTagList *tags;

  out_caps = gst_caps_new_empty_simple ("video/x-vp9");
  s = gst_caps_get_structure (out_caps, 0);

  out_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      out_caps, state);

  GST_INFO_OBJECT (self, "output caps: %" GST_PTR_FORMAT, out_state->caps);

  /* encoder will keep it around for us */
  gst_video_codec_state_unref (out_state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      gst_element_get_metadata (GST_ELEMENT_CAST (self),
          GST_ELEMENT_METADATA_LONGNAME), NULL);
  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (self), tags,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;

}

static void
gst_mf_vp9_enc_register (GstPlugin * plugin, guint rank,
    const gchar * device_name, const GstMFVP9EncDeviceCaps * device_caps,
    guint32 enum_flags, guint device_index,
    GstCaps * sink_caps, GstCaps * src_caps)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  gint i;
  GstMFVP9EncClassData *cdata;
  gboolean is_default = TRUE;
  GTypeInfo type_info = {
    sizeof (GstMFVP9EncClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_mf_vp9_enc_class_init,
    NULL,
    NULL,
    sizeof (GstMFVP9Enc),
    0,
    (GInstanceInitFunc) gst_mf_vp9_enc_init,
  };

  cdata = g_new0 (GstMFVP9EncClassData, 1);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = src_caps;
  cdata->device_name = g_strdup (device_name);
  cdata->device_caps = *device_caps;
  cdata->enum_flags = enum_flags;
  cdata->device_index = device_index;
  type_info.class_data = cdata;

  type_name = g_strdup ("GstMFVP9Enc");
  feature_name = g_strdup ("mfvp9enc");

  i = 1;
  while (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstMFVP9Device%dEnc", i);
    feature_name = g_strdup_printf ("mfvp9device%denc", i);
    is_default = FALSE;
    i++;
  }

  cdata->is_default = is_default;

  type =
      g_type_register_static (GST_TYPE_MF_VIDEO_ENC, type_name, &type_info,
      (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && !is_default)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}

static void
gst_mf_vp9_enc_plugin_init_internal (GstPlugin * plugin, guint rank,
    GstMFTransform * transform, guint device_index, guint32 enum_flags)
{
  HRESULT hr;
  MFT_REGISTER_TYPE_INFO *infos;
  UINT32 info_size;
  gint i;
  GstCaps *src_caps = NULL;
  GstCaps *sink_caps = NULL;
  GValue *supported_formats = NULL;
  gboolean have_I420 = FALSE;
  gchar *device_name = NULL;
  GstMFVP9EncDeviceCaps device_caps = { 0, };
  IMFActivate *activate;
  IMFTransform *encoder;
  ICodecAPI *codec_api;
  ComPtr<IMFMediaType> out_type;

  /* NOTE: depending on environment,
   * some enumerated h/w MFT might not be usable (e.g., multiple GPU case) */
  if (!gst_mf_transform_open (transform))
    return;

  activate = gst_mf_transform_get_activate_handle (transform);
  if (!activate) {
    GST_WARNING_OBJECT (transform, "No IMFActivate interface available");
    return;
  }

  encoder = gst_mf_transform_get_transform_handle (transform);
  if (!encoder) {
    GST_WARNING_OBJECT (transform, "No IMFTransform interface available");
    return;
  }

  codec_api = gst_mf_transform_get_codec_api_handle (transform);
  if (!codec_api) {
    GST_WARNING_OBJECT (transform, "No ICodecAPI interface available");
    return;
  }

  g_object_get (transform, "device-name", &device_name, NULL);
  if (!device_name) {
    GST_WARNING_OBJECT (transform, "Unknown device name");
    return;
  }

  hr = activate->GetAllocatedBlob (MFT_INPUT_TYPES_Attributes,
      (UINT8 **) & infos, &info_size);
  if (!gst_mf_result (hr))
    goto done;

  for (i = 0; i < info_size / sizeof (MFT_REGISTER_TYPE_INFO); i++) {
    GstVideoFormat vformat;
    GValue val = G_VALUE_INIT;

    vformat = gst_mf_video_subtype_to_video_format (&infos[i].guidSubtype);
    if (vformat == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    if (!supported_formats) {
      supported_formats = g_new0 (GValue, 1);
      g_value_init (supported_formats, GST_TYPE_LIST);
    }

    /* media foundation has duplicated formats IYUV and I420 */
    if (vformat == GST_VIDEO_FORMAT_I420) {
      if (have_I420)
        continue;

      have_I420 = TRUE;
    }

    g_value_init (&val, G_TYPE_STRING);
    g_value_set_static_string (&val, gst_video_format_to_string (vformat));

    gst_value_list_append_and_take_value (supported_formats, &val);
  }

  CoTaskMemFree (infos);

  if (!supported_formats)
    goto done;

  /* check supported resolutions */
  hr = MFCreateMediaType (out_type.GetAddressOf ());
  if (!gst_mf_result (hr))
    goto done;

  hr = out_type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (!gst_mf_result (hr))
    goto done;

  hr = out_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_VP90);
  if (!gst_mf_result (hr))
    goto done;

  hr = out_type->SetUINT32 (MF_MT_AVG_BITRATE, 2048000);
  if (!gst_mf_result (hr))
    goto done;

  hr = MFSetAttributeRatio (out_type.Get (), MF_MT_FRAME_RATE, 30, 1);
  if (!gst_mf_result (hr))
    goto done;

  hr = out_type->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (!gst_mf_result (hr))
    goto done;

  GST_DEBUG_OBJECT (transform, "Check supported profiles of %s",
      device_name);

  src_caps = gst_caps_new_empty_simple ("video/x-vp9");
  sink_caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_set_value (sink_caps, "format", supported_formats);
  g_value_unset (supported_formats);
  g_free (supported_formats);

  /* FIXME: don't hardcode resolution */
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 64, 8192,
      "height", GST_TYPE_INT_RANGE, 64, 8192, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 64, 8192,
      "height", GST_TYPE_INT_RANGE, 64, 8192, NULL);

  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

#define CHECK_DEVICE_CAPS(codec_obj,api,val) \
  if (SUCCEEDED((codec_obj)->IsSupported(&(api)))) {\
    device_caps.val = TRUE; \
  }

  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonRateControlMode, rc_mode);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonMaxBitRate, max_bitrate);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncCommonQualityVsSpeed, quality_vs_speed);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncMPVGOPSize, gop_size);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncNumWorkerThreads, threads);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoContentType, content_type);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncVideoForceKeyFrame, force_keyframe);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVLowLatencyMode, low_latency);

  gst_mf_vp9_enc_register (plugin, rank, device_name,
      &device_caps, enum_flags, device_index, sink_caps, src_caps);

done:
  g_free (device_name);
}

void
gst_mf_vp9_enc_plugin_init (GstPlugin * plugin, guint rank)
{
  GstMFTransformEnumParams enum_params = { 0, };
  MFT_REGISTER_TYPE_INFO output_type;
  GstMFTransform *transform;
  gint i;
  gboolean do_next;

  GST_DEBUG_CATEGORY_INIT (gst_mf_vp9_enc_debug, "mfvp9enc", 0, "mfvp9enc");

  output_type.guidMajorType = MFMediaType_Video;
  output_type.guidSubtype = MFVideoFormat_VP90;

  enum_params.category = MFT_CATEGORY_VIDEO_ENCODER;
  enum_params.enum_flags = (MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_ASYNCMFT |
      MFT_ENUM_FLAG_SORTANDFILTER  | MFT_ENUM_FLAG_SORTANDFILTER_APPROVED_ONLY);
  enum_params.output_typeinfo = &output_type;

  /* register hardware encoders first */
  i = 0;
  do {
    enum_params.device_index = i++;
    transform = gst_mf_transform_new (&enum_params);
    do_next = TRUE;

    if (!transform) {
      do_next = FALSE;
    } else {
      gst_mf_vp9_enc_plugin_init_internal (plugin, rank, transform,
          enum_params.device_index, enum_params.enum_flags);
      gst_clear_object (&transform);
    }
  } while (do_next);

  /* register software encoders */
  enum_params.enum_flags = (MFT_ENUM_FLAG_SYNCMFT |
      MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_SORTANDFILTER_APPROVED_ONLY);
  i = 0;
  do {
    enum_params.device_index = i++;
    transform = gst_mf_transform_new (&enum_params);
    do_next = TRUE;

    if (!transform) {
      do_next = FALSE;
    } else {
      gst_mf_vp9_enc_plugin_init_internal (plugin, rank, transform,
          enum_params.device_index, enum_params.enum_flags);
      gst_clear_object (&transform);
    }
  } while (do_next);
}
