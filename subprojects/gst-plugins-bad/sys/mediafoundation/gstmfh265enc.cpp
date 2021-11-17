/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha.yang@navercorp.com>
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
 * SECTION:element-mfh265enc
 * @title: mfh265enc
 *
 * This element encodes raw video into H265 (HEVC) compressed data.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v videotestsrc ! mfh265enc ! h265parse ! qtmux ! filesink location=videotestsrc.mp4
 * ]| This example pipeline will encode a test video source to H265 using
 * Media Foundation encoder, and muxes it in a mp4 container.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstmfvideoenc.h"
#include "gstmfh265enc.h"
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY (gst_mf_h265_enc_debug);
#define GST_CAT_DEFAULT gst_mf_h265_enc_debug

enum
{
  GST_MF_H265_ENC_RC_MODE_CBR = 0,
  GST_MF_H265_ENC_RC_MODE_QUALITY,
};

#define GST_TYPE_MF_H265_ENC_RC_MODE (gst_mf_h265_enc_rc_mode_get_type())
static GType
gst_mf_h265_enc_rc_mode_get_type (void)
{
  static GType rc_mode_type = 0;

  static const GEnumValue rc_mode_types[] = {
    {GST_MF_H265_ENC_RC_MODE_CBR, "Constant bitrate", "cbr"},
    {GST_MF_H265_ENC_RC_MODE_QUALITY, "Quality-based variable bitrate", "qvbr"},
    {0, NULL, NULL}
  };

  if (!rc_mode_type) {
    rc_mode_type = g_enum_register_static ("GstMFH265EncRCMode", rc_mode_types);
  }
  return rc_mode_type;
}

enum
{
  GST_MF_H265_ENC_CONTENT_TYPE_UNKNOWN,
  GST_MF_H265_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
};

#define GST_TYPE_MF_H265_ENC_CONTENT_TYPE (gst_mf_h265_enc_content_type_get_type())
static GType
gst_mf_h265_enc_content_type_get_type (void)
{
  static GType content_type = 0;

  static const GEnumValue content_types[] = {
    {GST_MF_H265_ENC_CONTENT_TYPE_UNKNOWN, "Unknown", "unknown"},
    {GST_MF_H265_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
        "Fixed Camera Angle, such as a webcam", "fixed"},
    {0, NULL, NULL}
  };

  if (!content_type) {
    content_type =
        g_enum_register_static ("GstMFH265EncContentType", content_types);
  }
  return content_type;
}

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_RC_MODE,
  PROP_BUFFER_SIZE,
  PROP_MAX_BITRATE,
  PROP_QUALITY_VS_SPEED,
  PROP_BFRAMES,
  PROP_GOP_SIZE,
  PROP_THREADS,
  PROP_CONTENT_TYPE,
  PROP_QP,
  PROP_LOW_LATENCY,
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_QP_I,
  PROP_QP_P,
  PROP_QP_B,
  PROP_REF,
  PROP_D3D11_AWARE,
  PROP_ADAPTER_LUID,
};

#define DEFAULT_BITRATE (2 * 1024)
#define DEFAULT_RC_MODE GST_MF_H265_ENC_RC_MODE_CBR
#define DEFAULT_BUFFER_SIZE 0
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_QUALITY_VS_SPEED 50
#define DEFAULT_CABAC TRUE
#define DEFAULT_BFRAMES 0
#define DEFAULT_GOP_SIZE -1
#define DEFAULT_THREADS 0
#define DEFAULT_CONTENT_TYPE GST_MF_H265_ENC_CONTENT_TYPE_UNKNOWN
#define DEFAULT_QP 24
#define DEFAULT_LOW_LATENCY FALSE
#define DEFAULT_MIN_QP 0
#define DEFAULT_MAX_QP 51
#define DEFAULT_QP_I 26
#define DEFAULT_QP_P 26
#define DEFAULT_QP_B 26
#define DEFAULT_REF 2

typedef struct _GstMFH265Enc
{
  GstMFVideoEnc parent;

  /* properties */
  guint bitrate;

  /* device dependent properties */
  guint rc_mode;
  guint buffer_size;
  guint max_bitrate;
  guint quality_vs_speed;
  guint bframes;
  guint gop_size;
  guint threads;
  guint content_type;
  guint qp;
  gboolean low_latency;
  guint min_qp;
  guint max_qp;
  guint qp_i;
  guint qp_p;
  guint qp_b;
  guint max_num_ref;
} GstMFH265Enc;

typedef struct _GstMFH265EncClass
{
  GstMFVideoEncClass parent_class;
} GstMFH265EncClass;

static GstElementClass *parent_class = NULL;

static void gst_mf_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mf_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_mf_h265_enc_set_option (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type);
static gboolean gst_mf_h265_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type);

static void
gst_mf_h265_enc_class_init (GstMFH265EncClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstMFVideoEncClass *mfenc_class = GST_MF_VIDEO_ENC_CLASS (klass);
  GstMFVideoEncClassData *cdata = (GstMFVideoEncClassData *) data;
  GstMFVideoEncDeviceCaps *device_caps = &cdata->device_caps;
  gchar *long_name;
  gchar *classification;

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_mf_h265_enc_get_property;
  gobject_class->set_property = gst_mf_h265_enc_set_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          (G_MAXUINT >> 10), DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  if (device_caps->rc_mode) {
    g_object_class_install_property (gobject_class, PROP_RC_MODE,
        g_param_spec_enum ("rc-mode", "Rate Control Mode",
            "Rate Control Mode",
            GST_TYPE_MF_H265_ENC_RC_MODE, DEFAULT_RC_MODE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_H265_ENC_RC_MODE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->buffer_size) {
    g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
        g_param_spec_uint ("vbv-buffer-size", "VBV Buffer Size",
            "VBV(HRD) Buffer Size in bytes (0 = MFT default)",
            0, G_MAXUINT - 1, DEFAULT_BUFFER_SIZE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->max_bitrate) {
    g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
        g_param_spec_uint ("max-bitrate", "Max Bitrate",
            "The maximum bitrate applied when rc-mode is \"pcvbr\" in kbit/sec "
            "(0 = MFT default)", 0, (G_MAXUINT >> 10), DEFAULT_MAX_BITRATE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->quality_vs_speed) {
    g_object_class_install_property (gobject_class, PROP_QUALITY_VS_SPEED,
        g_param_spec_uint ("quality-vs-speed", "Quality Vs Speed",
            "Quality and speed tradeoff, [0, 33]: Low complexity, "
            "[34, 66]: Medium complexity, [67, 100]: High complexity",
            0, 100, DEFAULT_QUALITY_VS_SPEED,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->bframes) {
    g_object_class_install_property (gobject_class, PROP_BFRAMES,
        g_param_spec_uint ("bframes", "bframes",
            "The maximum number of consecutive B frames",
            0, 2, DEFAULT_BFRAMES,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->gop_size) {
    g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
        g_param_spec_int ("gop-size", "GOP size",
            "The number of pictures from one GOP header to the next. "
            "Depending on GPU vendor implementation, zero gop-size might "
            "produce only one keyframe at the beginning (-1 for automatic)",
            -1, G_MAXINT, DEFAULT_GOP_SIZE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->threads) {
    g_object_class_install_property (gobject_class, PROP_THREADS,
        g_param_spec_uint ("threads", "Threads",
            "The number of worker threads used by a encoder, (0 = MFT default)",
            0, 16, DEFAULT_THREADS,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->content_type) {
    g_object_class_install_property (gobject_class, PROP_CONTENT_TYPE,
        g_param_spec_enum ("content-type", "Content Type",
            "Indicates the type of video content",
            GST_TYPE_MF_H265_ENC_CONTENT_TYPE, DEFAULT_CONTENT_TYPE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_H265_ENC_CONTENT_TYPE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->qp) {
    g_object_class_install_property (gobject_class, PROP_QP,
        g_param_spec_uint ("qp", "qp",
            "QP applied when rc-mode is \"qvbr\"", 16, 51, DEFAULT_QP,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->low_latency) {
    g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
        g_param_spec_boolean ("low-latency", "Low Latency",
            "Enable low latency encoding", DEFAULT_LOW_LATENCY,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->min_qp) {
    g_object_class_install_property (gobject_class, PROP_MIN_QP,
        g_param_spec_uint ("min-qp", "Min QP",
            "The minimum allowed QP applied to all rc-mode",
            0, 51, DEFAULT_MIN_QP,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->max_qp) {
    g_object_class_install_property (gobject_class, PROP_MAX_QP,
        g_param_spec_uint ("max-qp", "Max QP",
            "The maximum allowed QP applied to all rc-mode",
            0, 51, DEFAULT_MAX_QP,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->frame_type_qp) {
    g_object_class_install_property (gobject_class, PROP_QP_I,
        g_param_spec_uint ("qp-i", "QP I",
            "QP applied to I frames", 0, 51,
            DEFAULT_QP_I,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_QP_P,
        g_param_spec_uint ("qp-p", "QP P",
            "QP applied to P frames", 0, 51,
            DEFAULT_QP_P,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_QP_B,
        g_param_spec_uint ("qp-b", "QP B",
            "QP applied to B frames", 0, 51,
            DEFAULT_QP_B,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->max_num_ref) {
    g_object_class_install_property (gobject_class, PROP_REF,
        g_param_spec_uint ("ref", "Reference Frames",
            "The number of reference frames",
            device_caps->max_num_ref_low, device_caps->max_num_ref_high,
            DEFAULT_REF,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  /**
   * GstMFH265Enc:d3d11-aware:
   *
   * Whether element supports Direct3D11 texture as an input or not
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_D3D11_AWARE,
      g_param_spec_boolean ("d3d11-aware", "D3D11 Aware",
          "Whether device can support Direct3D11 interop",
          device_caps->d3d11_aware,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstMFH265Enc:adapter-luid:
   *
   * DXGI Adapter LUID for this elemenet
   *
   * Since: 1.20
   */
  if (device_caps->d3d11_aware) {
    g_object_class_install_property (gobject_class, PROP_ADAPTER_LUID,
        g_param_spec_int64 ("adapter-luid", "Adapter LUID",
            "DXGI Adapter LUID (Locally Unique Identifier) of created device",
            G_MININT64, G_MAXINT64, device_caps->adapter_luid,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  }

  long_name = g_strdup_printf ("Media Foundation %s", cdata->device_name);
  classification = g_strdup_printf ("Codec/Encoder/Video%s",
      (cdata->enum_flags & MFT_ENUM_FLAG_HARDWARE) == MFT_ENUM_FLAG_HARDWARE ?
      "/Hardware" : "");
  gst_element_class_set_metadata (element_class, long_name,
      classification,
      "Microsoft Media Foundation H.265 Encoder",
      "Seungha Yang <seungha.yang@navercorp.com>");
  g_free (long_name);
  g_free (classification);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  mfenc_class->set_option = GST_DEBUG_FUNCPTR (gst_mf_h265_enc_set_option);
  mfenc_class->set_src_caps = GST_DEBUG_FUNCPTR (gst_mf_h265_enc_set_src_caps);

  mfenc_class->codec_id = MFVideoFormat_HEVC;
  mfenc_class->enum_flags = cdata->enum_flags;
  mfenc_class->device_index = cdata->device_index;
  mfenc_class->device_caps = *device_caps;

  g_free (cdata->device_name);
  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_mf_h265_enc_init (GstMFH265Enc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->rc_mode = DEFAULT_RC_MODE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->quality_vs_speed = DEFAULT_QUALITY_VS_SPEED;
  self->bframes = DEFAULT_BFRAMES;
  self->gop_size = DEFAULT_GOP_SIZE;
  self->threads = DEFAULT_THREADS;
  self->content_type = DEFAULT_CONTENT_TYPE;
  self->qp = DEFAULT_QP;
  self->low_latency = DEFAULT_LOW_LATENCY;
  self->min_qp = DEFAULT_MIN_QP;
  self->max_qp = DEFAULT_MAX_QP;
  self->qp_i = DEFAULT_QP_I;
  self->qp_p = DEFAULT_QP_P;
  self->qp_b = DEFAULT_QP_B;
  self->max_num_ref = DEFAULT_REF;
}

static void
gst_mf_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFH265Enc *self = (GstMFH265Enc *) (object);
  GstMFVideoEncClass *klass = GST_MF_VIDEO_ENC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_RC_MODE:
      g_value_set_enum (value, self->rc_mode);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value, self->buffer_size);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, self->max_bitrate);
      break;
    case PROP_QUALITY_VS_SPEED:
      g_value_set_uint (value, self->quality_vs_speed);
      break;
    case PROP_BFRAMES:
      g_value_set_uint (value, self->bframes);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, self->gop_size);
      break;
    case PROP_THREADS:
      g_value_set_uint (value, self->threads);
      break;
    case PROP_CONTENT_TYPE:
      g_value_set_enum (value, self->content_type);
      break;
    case PROP_QP:
      g_value_set_uint (value, self->qp);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, self->low_latency);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, self->min_qp);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, self->max_qp);
      break;
    case PROP_QP_I:
      g_value_set_uint (value, self->qp_i);
      break;
    case PROP_QP_P:
      g_value_set_uint (value, self->qp_p);
      break;
    case PROP_QP_B:
      g_value_set_uint (value, self->qp_b);
      break;
    case PROP_REF:
      g_value_set_uint (value, self->max_num_ref);
      break;
    case PROP_D3D11_AWARE:
      g_value_set_boolean (value, klass->device_caps.d3d11_aware);
      break;
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, klass->device_caps.adapter_luid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFH265Enc *self = (GstMFH265Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case PROP_RC_MODE:
      self->rc_mode = g_value_get_enum (value);
      break;
    case PROP_BUFFER_SIZE:
      self->buffer_size = g_value_get_uint (value);
      break;
    case PROP_MAX_BITRATE:
      self->max_bitrate = g_value_get_uint (value);
      break;
    case PROP_QUALITY_VS_SPEED:
      self->quality_vs_speed = g_value_get_uint (value);
      break;
    case PROP_BFRAMES:
      self->bframes = g_value_get_uint (value);
      break;
    case PROP_GOP_SIZE:
      self->gop_size = g_value_get_int (value);
      break;
    case PROP_THREADS:
      self->threads = g_value_get_uint (value);
      break;
    case PROP_CONTENT_TYPE:
      self->content_type = g_value_get_enum (value);
      break;
    case PROP_QP:
      self->qp = g_value_get_uint (value);
      break;
    case PROP_LOW_LATENCY:
      self->low_latency = g_value_get_boolean (value);
      break;
    case PROP_MIN_QP:
      self->min_qp = g_value_get_uint (value);
      break;
    case PROP_MAX_QP:
      self->max_qp = g_value_get_uint (value);
      break;
    case PROP_QP_I:
      self->qp_i = g_value_get_uint (value);
      break;
    case PROP_QP_P:
      self->qp_p = g_value_get_uint (value);
      break;
    case PROP_QP_B:
      self->qp_b = g_value_get_uint (value);
      break;
    case PROP_REF:
      self->max_num_ref = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint
gst_mf_h265_enc_rc_mode_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_H265_ENC_RC_MODE_CBR:
      return eAVEncCommonRateControlMode_CBR;
    case GST_MF_H265_ENC_RC_MODE_QUALITY:
      return eAVEncCommonRateControlMode_Quality;
    default:
      return G_MAXUINT;
  }
}

static guint
gst_mf_h265_enc_content_type_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_H265_ENC_CONTENT_TYPE_UNKNOWN:
      return eAVEncVideoContentType_Unknown;
    case GST_MF_H265_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE:
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
gst_mf_h265_enc_set_option (GstMFVideoEnc * mfenc, GstVideoCodecState * state,
    IMFMediaType * output_type)
{
  GstMFH265Enc *self = (GstMFH265Enc *) mfenc;
  GstMFVideoEncClass *klass = GST_MF_VIDEO_ENC_GET_CLASS (mfenc);
  GstMFVideoEncDeviceCaps *device_caps = &klass->device_caps;
  HRESULT hr;
  GstMFTransform *transform = mfenc->transform;

  hr = output_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_HEVC);
  if (!gst_mf_result (hr))
    return FALSE;

  if (GST_VIDEO_INFO_FORMAT (&mfenc->input_state->info) ==
      GST_VIDEO_FORMAT_P010_10LE) {
    hr = output_type->SetUINT32 (MF_MT_MPEG2_PROFILE,
        eAVEncH265VProfile_Main_420_10);
  } else {
    hr = output_type->SetUINT32 (MF_MT_MPEG2_PROFILE,
        eAVEncH265VProfile_Main_420_8);
  }

  if (!gst_mf_result (hr))
    return FALSE;

  hr = output_type->SetUINT32 (MF_MT_AVG_BITRATE,
      MIN (self->bitrate * 1024, G_MAXUINT - 1));
  if (!gst_mf_result (hr))
    return FALSE;

  if (device_caps->rc_mode) {
    guint rc_mode;
    rc_mode = gst_mf_h265_enc_rc_mode_to_enum (self->rc_mode);
    if (rc_mode != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncCommonRateControlMode, rc_mode);
      WARNING_HR (hr, CODECAPI_AVEncCommonRateControlMode);
    }
  }

  if (device_caps->buffer_size && self->buffer_size > 0) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonBufferSize, self->buffer_size);
    WARNING_HR (hr, CODECAPI_AVEncCommonBufferSize);
  }

  if (device_caps->max_bitrate && self->max_bitrate > 0) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonMaxBitRate,
        MIN (self->max_bitrate * 1024, G_MAXUINT - 1));
    WARNING_HR (hr, CODECAPI_AVEncCommonMaxBitRate);
  }

  if (device_caps->quality_vs_speed) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonQualityVsSpeed, self->quality_vs_speed);
    WARNING_HR (hr, CODECAPI_AVEncCommonQualityVsSpeed);
  }

  mfenc->has_reorder_frame = FALSE;
  if (device_caps->bframes) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncMPVDefaultBPictureCount, self->bframes);
    if (SUCCEEDED (hr) && self->bframes > 0)
      mfenc->has_reorder_frame = TRUE;

    WARNING_HR (hr, CODECAPI_AVEncMPVDefaultBPictureCount);
  }

  if (device_caps->gop_size) {
    GstVideoInfo *info = &state->info;
    gint gop_size = self->gop_size;
    gint fps_n, fps_d;

    /* Set default value (10 sec or 250 frames) like that of x264enc */
    if (gop_size < 0) {
      fps_n = GST_VIDEO_INFO_FPS_N (info);
      fps_d = GST_VIDEO_INFO_FPS_D (info);
      if (fps_n <= 0 || fps_d <= 0) {
        gop_size = 250;
      } else {
        gop_size = 10 * fps_n / fps_d;
      }

      GST_DEBUG_OBJECT (self, "Update GOP size to %d", gop_size);
    }

    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncMPVGOPSize, gop_size);
    WARNING_HR (hr, CODECAPI_AVEncMPVGOPSize);
  }

  if (device_caps->threads) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncNumWorkerThreads, self->threads);
    WARNING_HR (hr, CODECAPI_AVEncNumWorkerThreads);
  }

  if (device_caps->content_type) {
    guint content_type;
    content_type = gst_mf_h265_enc_content_type_to_enum (self->content_type);
    if (content_type != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncVideoContentType, content_type);
      WARNING_HR (hr, CODECAPI_AVEncVideoContentType);
    }
  }

  if (device_caps->qp) {
    hr = gst_mf_transform_set_codec_api_uint64 (transform,
        &CODECAPI_AVEncVideoEncodeQP, self->qp);
    WARNING_HR (hr, CODECAPI_AVEncVideoEncodeQP);
  }

  if (device_caps->low_latency) {
    hr = gst_mf_transform_set_codec_api_boolean (transform,
        &CODECAPI_AVLowLatencyMode, self->low_latency);
    WARNING_HR (hr, CODECAPI_AVLowLatencyMode);
  }

  if (device_caps->min_qp) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncVideoMinQP, self->min_qp);
    WARNING_HR (hr, CODECAPI_AVEncVideoMinQP);
  }

  if (device_caps->max_qp) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncVideoMaxQP, self->max_qp);
    WARNING_HR (hr, CODECAPI_AVEncVideoMaxQP);
  }

  if (device_caps->frame_type_qp) {
    guint64 type_qp = 0;

    type_qp =
        (guint64) self->qp_i | (guint64) self->qp_p << 16 |
        (guint64) self->qp_b << 32;
    hr = gst_mf_transform_set_codec_api_uint64 (transform,
        &CODECAPI_AVEncVideoEncodeFrameTypeQP, type_qp);
    WARNING_HR (hr, CODECAPI_AVEncVideoEncodeFrameTypeQP);
  }

  if (device_caps->max_num_ref) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncVideoMaxNumRefFrame, self->max_num_ref);
    WARNING_HR (hr, CODECAPI_AVEncVideoMaxNumRefFrame);
  }

  return TRUE;
}

static gboolean
gst_mf_h265_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type)
{
  GstMFH265Enc *self = (GstMFH265Enc *) mfenc;
  GstVideoCodecState *out_state;
  GstStructure *s;
  GstCaps *out_caps;
  GstTagList *tags;

  out_caps = gst_caps_new_empty_simple ("video/x-h265");
  s = gst_caps_get_structure (out_caps, 0);

  gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);

  if (GST_VIDEO_INFO_FORMAT (&mfenc->input_state->info) ==
      GST_VIDEO_FORMAT_P010_10LE) {
    gst_structure_set (s, "profile", G_TYPE_STRING, "main-10", NULL);
  } else {
    gst_structure_set (s, "profile", G_TYPE_STRING, "main", NULL);
  }

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

void
gst_mf_h265_enc_plugin_init (GstPlugin * plugin, guint rank,
    GList * d3d11_device)
{
  GTypeInfo type_info = {
    sizeof (GstMFH265EncClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_mf_h265_enc_class_init,
    NULL,
    NULL,
    sizeof (GstMFH265Enc),
    0,
    (GInstanceInitFunc) gst_mf_h265_enc_init,
  };
  GUID subtype = MFVideoFormat_HEVC;

  GST_DEBUG_CATEGORY_INIT (gst_mf_h265_enc_debug, "mfh265enc", 0, "mfh265enc");

  gst_mf_video_enc_register (plugin, rank, &subtype, &type_info, d3d11_device);
}
