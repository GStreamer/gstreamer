/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
 * SECTION:element-mfh264enc
 * @title: mfh264enc
 *
 * This element encodes raw video into H264 compressed data.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v videotestsrc ! mfh264enc ! h264parse ! qtmux ! filesink location=videotestsrc.mp4
 * ]| This example pipeline will encode a test video source to H264 using
 * Media Foundation encoder, and muxes it in a mp4 container.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "gstmfvideoenc.h"
#include "gstmfh264enc.h"
#include <wrl.h>

using namespace Microsoft::WRL;

GST_DEBUG_CATEGORY (gst_mf_h264_enc_debug);
#define GST_CAT_DEFAULT gst_mf_h264_enc_debug

enum
{
  GST_MF_H264_ENC_RC_MODE_CBR = 0,
  GST_MF_H264_ENC_RC_MODE_PEAK_CONSTRAINED_VBR,
  GST_MF_H264_ENC_RC_MODE_UNCONSTRAINED_VBR,
  GST_MF_H264_ENC_RC_MODE_QUALITY,
};

#define GST_TYPE_MF_H264_ENC_RC_MODE (gst_mf_h264_enc_rc_mode_get_type())
static GType
gst_mf_h264_enc_rc_mode_get_type (void)
{
  static GType rc_mode_type = 0;

  static const GEnumValue rc_mode_types[] = {
    {GST_MF_H264_ENC_RC_MODE_CBR, "Constant bitrate", "cbr"},
    {GST_MF_H264_ENC_RC_MODE_PEAK_CONSTRAINED_VBR,
        "Peak Constrained variable bitrate", "pcvbr"},
    {GST_MF_H264_ENC_RC_MODE_UNCONSTRAINED_VBR,
        "Unconstrained variable bitrate", "uvbr"},
    {GST_MF_H264_ENC_RC_MODE_QUALITY, "Quality-based variable bitrate", "qvbr"},
    {0, NULL, NULL}
  };

  if (!rc_mode_type) {
    rc_mode_type = g_enum_register_static ("GstMFH264EncRCMode", rc_mode_types);
  }
  return rc_mode_type;
}

enum
{
  GST_MF_H264_ENC_ADAPTIVE_MODE_NONE,
  GST_MF_H264_ENC_ADAPTIVE_MODE_FRAMERATE,
};

#define GST_TYPE_MF_H264_ENC_ADAPTIVE_MODE (gst_mf_h264_enc_adaptive_mode_get_type())
static GType
gst_mf_h264_enc_adaptive_mode_get_type (void)
{
  static GType adaptive_mode_type = 0;

  static const GEnumValue adaptive_mode_types[] = {
    {GST_MF_H264_ENC_ADAPTIVE_MODE_NONE, "None", "none"},
    {GST_MF_H264_ENC_ADAPTIVE_MODE_FRAMERATE,
        "Adaptively change the frame rate", "framerate"},
    {0, NULL, NULL}
  };

  if (!adaptive_mode_type) {
    adaptive_mode_type =
        g_enum_register_static ("GstMFH264EncAdaptiveMode",
        adaptive_mode_types);
  }
  return adaptive_mode_type;
}

enum
{
  GST_MF_H264_ENC_CONTENT_TYPE_UNKNOWN,
  GST_MF_H264_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
};

#define GST_TYPE_MF_H264_ENC_CONTENT_TYPE (gst_mf_h264_enc_content_type_get_type())
static GType
gst_mf_h264_enc_content_type_get_type (void)
{
  static GType content_type = 0;

  static const GEnumValue content_types[] = {
    {GST_MF_H264_ENC_CONTENT_TYPE_UNKNOWN, "Unknown", "unknown"},
    {GST_MF_H264_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
        "Fixed Camera Angle, such as a webcam", "fixed"},
    {0, NULL, NULL}
  };

  if (!content_type) {
    content_type =
        g_enum_register_static ("GstMFH264EncContentType", content_types);
  }
  return content_type;
}

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_RC_MODE,
  PROP_QUALITY,
  PROP_ADAPTIVE_MODE,
  PROP_BUFFER_SIZE,
  PROP_MAX_BITRATE,
  PROP_QUALITY_VS_SPEED,
  PROP_CABAC,
  PROP_SPS_ID,
  PROP_PPS_ID,
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
};

#define DEFAULT_BITRATE (2 * 1024)
#define DEFAULT_RC_MODE GST_MF_H264_ENC_RC_MODE_UNCONSTRAINED_VBR
#define DEFAULT_QUALITY_LEVEL 70
#define DEFAULT_ADAPTIVE_MODE GST_MF_H264_ENC_ADAPTIVE_MODE_NONE
#define DEFAULT_BUFFER_SIZE 0
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_QUALITY_VS_SPEED 50
#define DEFAULT_CABAC TRUE
#define DEFAULT_SPS_ID 0
#define DEFAULT_PPS_ID 0
#define DEFAULT_BFRAMES 0
#define DEFAULT_GOP_SIZE 0
#define DEFAULT_THREADS 0
#define DEFAULT_CONTENT_TYPE GST_MF_H264_ENC_CONTENT_TYPE_UNKNOWN
#define DEFAULT_QP 24
#define DEFAULT_LOW_LATENCY FALSE
#define DEFAULT_MIN_QP 0
#define DEFAULT_MAX_QP 51
#define DEFAULT_QP_I 26
#define DEFAULT_QP_P 26
#define DEFAULT_QP_B 26
#define DEFAULT_REF 2

#define GST_MF_H264_ENC_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE (obj), GstMFH264EncClass))

typedef struct _GstMFH264EncDeviceCaps
{
  /* if CodecAPI is available */
  gboolean rc_mode; /* AVEncCommonRateControlMode */
  gboolean quality; /* AVEncCommonQuality */

  gboolean adaptive_mode;     /* AVEncAdaptiveMode */
  gboolean buffer_size;       /* AVEncCommonBufferSize */
  gboolean max_bitrate;       /* AVEncCommonMaxBitRate */
  gboolean quality_vs_speed;  /* AVEncCommonQualityVsSpeed */
  gboolean cabac;             /* AVEncH264CABACEnable */
  gboolean sps_id;            /* AVEncH264SPSID */
  gboolean pps_id;            /* AVEncH264PPSID */
  gboolean bframes;            /* AVEncMPVDefaultBPictureCount */
  gboolean gop_size;          /* AVEncMPVGOPSize */
  gboolean threads;           /* AVEncNumWorkerThreads */
  gboolean content_type;      /* AVEncVideoContentType */
  gboolean qp;                /* AVEncVideoEncodeQP */
  gboolean force_keyframe;    /* AVEncVideoForceKeyFrame */
  gboolean low_latency;       /* AVLowLatencyMode */

  /* since Windows 8.1 */
  gboolean min_qp;        /* AVEncVideoMinQP */
  gboolean max_qp;        /* AVEncVideoMaxQP */
  gboolean frame_type_qp; /* AVEncVideoEncodeFrameTypeQP */
  gboolean max_num_ref;   /* AVEncVideoMaxNumRefFrame */
  guint max_num_ref_high;
  guint max_num_ref_low;
} GstMFH264EncDeviceCaps;

typedef struct _GstMFH264Enc
{
  GstMFVideoEnc parent;

  /* properteies */
  guint bitrate;

  /* device dependent properties */
  guint rc_mode;
  guint quality;
  guint adaptive_mode;
  guint buffer_size;
  guint max_bitrate;
  guint quality_vs_speed;
  gboolean cabac;
  guint sps_id;
  guint pps_id;
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
} GstMFH264Enc;

typedef struct _GstMFH264EncClass
{
  GstMFVideoEncClass parent_class;

  GstMFH264EncDeviceCaps device_caps;
} GstMFH264EncClass;

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gchar *device_name;
  guint32 enum_flags;
  guint device_index;
  GstMFH264EncDeviceCaps device_caps;
  gboolean is_default;
} GstMFH264EncClassData;

static GstElementClass *parent_class = NULL;

static void gst_mf_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mf_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_mf_h264_enc_set_option (GstMFVideoEnc * mfenc,
    IMFMediaType * output_type);
static gboolean gst_mf_h264_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type);

static void
gst_mf_h264_enc_class_init (GstMFH264EncClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstMFVideoEncClass *mfenc_class = GST_MF_VIDEO_ENC_CLASS (klass);
  GstMFH264EncClassData *cdata = (GstMFH264EncClassData *) data;
  GstMFH264EncDeviceCaps *device_caps = &cdata->device_caps;
  gchar *long_name;
  gchar *classification;

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  klass->device_caps = *device_caps;

  gobject_class->get_property = gst_mf_h264_enc_get_property;
  gobject_class->set_property = gst_mf_h264_enc_set_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          (G_MAXUINT >> 10), DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  if (device_caps->rc_mode) {
    g_object_class_install_property (gobject_class, PROP_RC_MODE,
        g_param_spec_enum ("rc-mode", "Rate Control Mode",
            "Rate Control Mode",
            GST_TYPE_MF_H264_ENC_RC_MODE, DEFAULT_RC_MODE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_H264_ENC_RC_MODE,
          (GstPluginAPIFlags) 0);
    }
  }

  /* quality and qp has the identical meaning but scale is different
   * use qp if available */
  if (device_caps->quality && !device_caps->qp) {
    g_object_class_install_property (gobject_class, PROP_QUALITY,
        g_param_spec_uint ("quality", "Quality",
            "Quality applied when rc-mode is qvbr",
            1, 100, DEFAULT_QUALITY_LEVEL,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->adaptive_mode) {
    g_object_class_install_property (gobject_class, PROP_ADAPTIVE_MODE,
        g_param_spec_enum ("adaptive-mode", "Adaptive Mode",
            "Adaptive Mode", GST_TYPE_MF_H264_ENC_ADAPTIVE_MODE,
            DEFAULT_ADAPTIVE_MODE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_H264_ENC_ADAPTIVE_MODE,
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
            "The maximum bitrate applied when rc-mode is \"pcvbr\" in kbit/sec",
            0, (G_MAXUINT >> 10), DEFAULT_MAX_BITRATE,
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

  if (device_caps->cabac) {
    g_object_class_install_property (gobject_class, PROP_CABAC,
        g_param_spec_boolean ("cabac", "Use CABAC",
            "Enable CABAC entropy coding",
            DEFAULT_CABAC,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->sps_id) {
    g_object_class_install_property (gobject_class, PROP_SPS_ID,
        g_param_spec_uint ("sps-id", "SPS Id",
            "The SPS id to use", 0, 31,
            DEFAULT_SPS_ID,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->pps_id) {
    g_object_class_install_property (gobject_class, PROP_PPS_ID,
        g_param_spec_uint ("pps-id", "PPS Id",
            "The PPS id to use", 0, 255,
            DEFAULT_PPS_ID,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->bframes) {
    g_object_class_install_property (gobject_class, PROP_BFRAMES,
        g_param_spec_uint ("bframes", "bframes",
            "The maximum number of consecutive B frames", 0, 2,
            DEFAULT_BFRAMES,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->gop_size) {
    g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
        g_param_spec_uint ("gop-size", "GOP size",
            "The number of pictures from one GOP header to the next, "
            "(0 = MFT default)", 0, G_MAXUINT - 1, DEFAULT_GOP_SIZE,
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
            GST_TYPE_MF_H264_ENC_CONTENT_TYPE, DEFAULT_CONTENT_TYPE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_H264_ENC_CONTENT_TYPE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->qp) {
    g_object_class_install_property (gobject_class, PROP_QP,
        g_param_spec_uint ("qp", "qp",
            "QP applied when rc-mode is \"qvbr\"", 16, 51,
            DEFAULT_QP,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->low_latency) {
    g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
        g_param_spec_boolean ("low-latency", "Low Latency",
            "Enable low latency encoding",
            DEFAULT_LOW_LATENCY,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->min_qp) {
    g_object_class_install_property (gobject_class, PROP_MIN_QP,
        g_param_spec_uint ("min-qp", "Min QP",
            "The minimum allowed QP applied to all rc-mode", 0, 51,
            DEFAULT_MIN_QP,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->max_qp) {
    g_object_class_install_property (gobject_class, PROP_MAX_QP,
        g_param_spec_uint ("max-qp", "Max QP",
            "The maximum allowed QP applied to all rc-mode", 0, 51,
            DEFAULT_MAX_QP,
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

  long_name = g_strdup_printf ("Media Foundation %s", cdata->device_name);
  classification = g_strdup_printf ("Codec/Encoder/Video%s",
      (cdata->enum_flags & MFT_ENUM_FLAG_HARDWARE) == MFT_ENUM_FLAG_HARDWARE ?
          "/Hardware" : "");
  gst_element_class_set_metadata (element_class, long_name,
      classification,
      "Microsoft Media Foundation H.264 Encoder",
      "Seungha Yang <seungha.yang@navercorp.com>");
  g_free (long_name);
  g_free (classification);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  mfenc_class->set_option = GST_DEBUG_FUNCPTR (gst_mf_h264_enc_set_option);
  mfenc_class->set_src_caps = GST_DEBUG_FUNCPTR (gst_mf_h264_enc_set_src_caps);

  mfenc_class->codec_id = MFVideoFormat_H264;
  mfenc_class->enum_flags = cdata->enum_flags;
  mfenc_class->device_index = cdata->device_index;
  mfenc_class->can_force_keyframe = device_caps->force_keyframe;

  g_free (cdata->device_name);
  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_mf_h264_enc_init (GstMFH264Enc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->rc_mode = DEFAULT_RC_MODE;
  self->quality = DEFAULT_QUALITY_LEVEL;
  self->adaptive_mode = DEFAULT_ADAPTIVE_MODE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->quality_vs_speed = DEFAULT_QUALITY_VS_SPEED;
  self->cabac = DEFAULT_CABAC;
  self->sps_id = DEFAULT_SPS_ID;
  self->pps_id = DEFAULT_PPS_ID;
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
gst_mf_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFH264Enc *self = (GstMFH264Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_RC_MODE:
      g_value_set_enum (value, self->rc_mode);
      break;
    case PROP_QUALITY:
      g_value_set_uint (value, self->quality);
      break;
    case PROP_ADAPTIVE_MODE:
      g_value_set_enum (value, self->adaptive_mode);
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
    case PROP_CABAC:
      g_value_set_boolean (value, self->cabac);
      break;
    case PROP_SPS_ID:
      g_value_set_uint (value, self->sps_id);
      break;
    case PROP_PPS_ID:
      g_value_set_uint (value, self->pps_id);
      break;
    case PROP_BFRAMES:
      g_value_set_uint (value, self->bframes);
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFH264Enc *self = (GstMFH264Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case PROP_RC_MODE:
      self->rc_mode = g_value_get_enum (value);
      break;
    case PROP_QUALITY:
      self->quality = g_value_get_uint (value);
      break;
    case PROP_ADAPTIVE_MODE:
      self->adaptive_mode = g_value_get_enum (value);
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
    case PROP_CABAC:
      self->cabac = g_value_get_boolean (value);
      break;
    case PROP_SPS_ID:
      self->sps_id = g_value_get_uint (value);
      break;
    case PROP_PPS_ID:
      self->pps_id = g_value_get_uint (value);
      break;
    case PROP_BFRAMES:
      self->bframes = g_value_get_uint (value);
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
gst_mf_h264_enc_rc_mode_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_H264_ENC_RC_MODE_CBR:
      return eAVEncCommonRateControlMode_CBR;
    case GST_MF_H264_ENC_RC_MODE_PEAK_CONSTRAINED_VBR:
      return eAVEncCommonRateControlMode_PeakConstrainedVBR;
    case GST_MF_H264_ENC_RC_MODE_UNCONSTRAINED_VBR:
      return eAVEncCommonRateControlMode_UnconstrainedVBR;
    case GST_MF_H264_ENC_RC_MODE_QUALITY:
      return eAVEncCommonRateControlMode_Quality;
    default:
      return G_MAXUINT;
  }
}

static guint
gst_mf_h264_enc_adaptive_mode_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_H264_ENC_ADAPTIVE_MODE_NONE:
      return eAVEncAdaptiveMode_None;
    case GST_MF_H264_ENC_ADAPTIVE_MODE_FRAMERATE:
      return eAVEncAdaptiveMode_FrameRate;
    default:
      return G_MAXUINT;
  }
}

static guint
gst_mf_h264_enc_content_type_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_H264_ENC_CONTENT_TYPE_UNKNOWN:
      return eAVEncVideoContentType_Unknown;
    case GST_MF_H264_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE:
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
gst_mf_h264_enc_set_option (GstMFVideoEnc * mfenc, IMFMediaType * output_type)
{
  GstMFH264Enc *self = (GstMFH264Enc *) mfenc;
  GstMFH264EncClass *klass = GST_MF_H264_ENC_GET_CLASS (self);
  GstMFH264EncDeviceCaps *device_caps = &klass->device_caps;
  HRESULT hr;
  GstCaps *allowed_caps, *template_caps;
  guint selected_profile = eAVEncH264VProfile_Main;
  gint level_idc = -1;
  GstMFTransform *transform = mfenc->transform;

  template_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (self));
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (self));

  if (template_caps == allowed_caps) {
    GST_INFO_OBJECT (self, "downstream has ANY caps");
  } else if (allowed_caps) {
    GstStructure *s;
    const gchar *profile;
    const gchar *level;

    if (gst_caps_is_empty (allowed_caps)) {
      gst_caps_unref (allowed_caps);
      gst_caps_unref (template_caps);
      return FALSE;
    }

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);

    profile = gst_structure_get_string (s, "profile");
    if (profile) {
      if (!strcmp (profile, "baseline")) {
        selected_profile = eAVEncH264VProfile_Base;
      } else if (g_str_has_prefix (profile, "high")) {
        selected_profile = eAVEncH264VProfile_High;
      } else if (g_str_has_prefix (profile, "main")) {
        selected_profile = eAVEncH264VProfile_Main;
      }
    }

    level = gst_structure_get_string (s, "level");
    if (level)
      level_idc = gst_codec_utils_h264_get_level_idc (level);

    gst_caps_unref (allowed_caps);
  }
  gst_caps_unref (template_caps);

  hr = output_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = output_type->SetUINT32 (MF_MT_MPEG2_PROFILE, selected_profile);
  if (!gst_mf_result (hr))
    return FALSE;

  if (level_idc >= eAVEncH264VLevel1 && level_idc <= eAVEncH264VLevel5_2) {
    hr = output_type->SetUINT32 (MF_MT_MPEG2_LEVEL, level_idc);
    if (!gst_mf_result (hr))
      return FALSE;
  }

  hr = output_type->SetUINT32 (MF_MT_AVG_BITRATE,
      MIN (self->bitrate * 1024, G_MAXUINT - 1));
  if (!gst_mf_result (hr))
    return FALSE;

  if (device_caps->rc_mode) {
    guint rc_mode;
    rc_mode = gst_mf_h264_enc_rc_mode_to_enum (self->rc_mode);
    if (rc_mode != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncCommonRateControlMode, rc_mode);
      WARNING_HR (hr, CODECAPI_AVEncCommonRateControlMode);
    }
  }

  if (device_caps->quality && !device_caps->qp) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonQuality, self->quality);
    WARNING_HR (hr, CODECAPI_AVEncCommonQuality);
  }

  if (device_caps->adaptive_mode) {
    guint adaptive_mode;
    adaptive_mode =
        gst_mf_h264_enc_adaptive_mode_to_enum (self->adaptive_mode);
    if (adaptive_mode != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncAdaptiveMode, adaptive_mode);
      WARNING_HR (hr, CODECAPI_AVEncAdaptiveMode);
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
        &CODECAPI_AVEncCommonQualityVsSpeed,
        self->quality_vs_speed);
    WARNING_HR (hr, CODECAPI_AVEncCommonQualityVsSpeed);
  }

  if (device_caps->cabac && selected_profile != eAVEncH264VProfile_Base) {
    hr = gst_mf_transform_set_codec_api_boolean (transform,
        &CODECAPI_AVEncH264CABACEnable, self->cabac);
    WARNING_HR (hr, CODECAPI_AVEncH264CABACEnable);
  }

  if (device_caps->sps_id) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncH264SPSID, self->sps_id);
    WARNING_HR (hr, CODECAPI_AVEncH264SPSID);
  }

  if (device_caps->pps_id) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncH264PPSID, self->pps_id);
    WARNING_HR (hr, CODECAPI_AVEncH264PPSID);
  }

  if (device_caps->bframes && selected_profile != eAVEncH264VProfile_Base) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncMPVDefaultBPictureCount, self->bframes);
    WARNING_HR (hr, CODECAPI_AVEncMPVDefaultBPictureCount);
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
    content_type = gst_mf_h264_enc_content_type_to_enum (self->content_type);
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
gst_mf_h264_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type)
{
  GstMFH264Enc *self = (GstMFH264Enc *) mfenc;
  GstVideoCodecState *out_state;
  GstStructure *s;
  GstCaps *out_caps;
  GstTagList *tags;

  out_caps = gst_caps_new_empty_simple ("video/x-h264");
  s = gst_caps_get_structure (out_caps, 0);

  gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);

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
gst_mf_h264_enc_register (GstPlugin * plugin, guint rank,
    const gchar * device_name, const GstMFH264EncDeviceCaps * device_caps,
    guint32 enum_flags, guint device_index,
    GstCaps * sink_caps, GstCaps * src_caps)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  gint i;
  GstMFH264EncClassData *cdata;
  gboolean is_default = TRUE;
  GTypeInfo type_info = {
    sizeof (GstMFH264EncClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_mf_h264_enc_class_init,
    NULL,
    NULL,
    sizeof (GstMFH264Enc),
    0,
    (GInstanceInitFunc) gst_mf_h264_enc_init,
  };

  cdata = g_new0 (GstMFH264EncClassData, 1);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = src_caps;
  cdata->device_name = g_strdup (device_name);
  cdata->device_caps = *device_caps;
  cdata->enum_flags = enum_flags;
  cdata->device_index = device_index;
  type_info.class_data = cdata;

  type_name = g_strdup ("GstMFH264Enc");
  feature_name = g_strdup ("mfh264enc");

  i = 1;
  while (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstMFH264Device%dEnc", i);
    feature_name = g_strdup_printf ("mfh264device%denc", i);
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

typedef struct
{
  guint width;
  guint height;
} GstMFH264EncResolution;

typedef struct
{
  eAVEncH264VProfile profile;
  const gchar *profile_str;
} GStMFH264EncProfileMap;

static void
gst_mf_h264_enc_plugin_init_internal (GstPlugin * plugin, guint rank,
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
  GstMFH264EncDeviceCaps device_caps = { 0, };
  IMFActivate *activate;
  IMFTransform *encoder;
  ICodecAPI *codec_api;
  ComPtr<IMFMediaType> out_type;
  GstMFH264EncResolution resolutions_to_check[] = {
    {1920, 1088}, {2560, 1440}, {3840, 2160}, {4096, 2160}, {8192, 4320}
  };
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  GStMFH264EncProfileMap profiles_to_check[] = {
    { eAVEncH264VProfile_High, "high" },
    { eAVEncH264VProfile_Main, "main" },
    { eAVEncH264VProfile_Base, "baseline" },
  };
  guint num_profiles = 0;
  GValue profiles = G_VALUE_INIT;

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

  g_value_init (&profiles, GST_TYPE_LIST);

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

  /* check supported profiles and resolutions */
  hr = MFCreateMediaType (out_type.GetAddressOf ());
  if (!gst_mf_result (hr))
    goto done;

  hr = out_type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (!gst_mf_result (hr))
    goto done;

  hr = out_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
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
  for (i = 0; i < G_N_ELEMENTS (profiles_to_check); i++) {
    GValue profile_val = G_VALUE_INIT;

    hr = out_type->SetUINT32 (MF_MT_MPEG2_PROFILE,
        profiles_to_check[i].profile);
    if (!gst_mf_result (hr))
      goto done;

    hr = MFSetAttributeSize (out_type.Get (), MF_MT_FRAME_SIZE,
        resolutions_to_check[0].width, resolutions_to_check[0].height);
    if (!gst_mf_result (hr))
      break;

    if (!gst_mf_transform_set_output_type (transform, out_type.Get ()))
      break;

    GST_DEBUG_OBJECT (transform, "MFT supports h264 %s profile",
        profiles_to_check[i].profile_str);

    g_value_init (&profile_val, G_TYPE_STRING);
    g_value_set_static_string (&profile_val, profiles_to_check[i].profile_str);
    gst_value_list_append_and_take_value (&profiles, &profile_val);
    num_profiles++;

    /* clear media type */
    gst_mf_transform_set_output_type (transform, NULL);
  }

  if (num_profiles == 0) {
    GST_WARNING_OBJECT (transform, "Couldn't query supported profile");
    goto done;
  }

  /* baseline is default profile */
  hr = out_type->SetUINT32 (MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
  if (!gst_mf_result (hr))
    goto done;

  GST_DEBUG_OBJECT (transform, "Check supported resolutions of %s",
      device_name);

  /* FIXME: This would take so long time.
   * Need to find smart way to find supported resolution*/
#if 0
  for (i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
    guint width, height;

    width = resolutions_to_check[i].width;
    height = resolutions_to_check[i].height;

    hr = MFSetAttributeSize (out_type.Get (), MF_MT_FRAME_SIZE, width, height);
    if (!gst_mf_result (hr))
      break;

    if (!gst_mf_transform_set_output_type (transform, out_type.Get ()))
      break;

    max_width = width;
    max_height = height;

    GST_DEBUG_OBJECT (transform,
        "MFT supports resolution %dx%d", max_width, max_height);

    /* clear media type */
    gst_mf_transform_set_output_type (transform, NULL);
  }

  if (max_width == 0 || max_height == 0) {
    GST_WARNING_OBJECT (transform, "Couldn't query supported resolution");
    goto done;
  }
#else
  /* FIXME: don't hardcode supported resolution */
  max_width = max_height = 8192;
#endif

  /* high profile supported since windows8 */
  src_caps = gst_caps_from_string ("video/x-h264, "
      "stream-format=(string) byte-stream, "
      "alignment=(string) au");
  gst_caps_set_value (src_caps, "profile", &profiles);

  sink_caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_set_value (sink_caps, "format", supported_formats);
  g_value_unset (supported_formats);
  g_free (supported_formats);

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);

  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

#define CHECK_DEVICE_CAPS(codec_obj,api,val) \
  if (SUCCEEDED((codec_obj)->IsSupported(&(api)))) {\
    device_caps.val = TRUE; \
  }

  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonRateControlMode, rc_mode);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonQuality, quality);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncAdaptiveMode, adaptive_mode);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonBufferSize, buffer_size);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonMaxBitRate, max_bitrate);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncCommonQualityVsSpeed, quality_vs_speed);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncH264CABACEnable, cabac);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncH264SPSID, sps_id);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncH264PPSID, pps_id);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncMPVDefaultBPictureCount, bframes);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncMPVGOPSize, gop_size);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncNumWorkerThreads, threads);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoContentType, content_type);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoEncodeQP, qp);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncVideoForceKeyFrame, force_keyframe);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVLowLatencyMode, low_latency);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoMinQP, min_qp);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoMaxQP, max_qp);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncVideoEncodeFrameTypeQP, frame_type_qp);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoMaxNumRefFrame, max_num_ref);
  if (device_caps.max_num_ref) {
    VARIANT min;
    VARIANT max;
    VARIANT step;

    hr = codec_api->GetParameterRange (&CODECAPI_AVEncVideoMaxNumRefFrame,
        &min, &max, &step);
    if (SUCCEEDED (hr)) {
      device_caps.max_num_ref = TRUE;
      device_caps.max_num_ref_high = max.uiVal;
      device_caps.max_num_ref_low = min.uiVal;
      VariantClear (&min);
      VariantClear (&max);
      VariantClear (&step);
    }
  }

  gst_mf_h264_enc_register (plugin, rank, device_name,
      &device_caps, enum_flags, device_index, sink_caps, src_caps);

done:
  g_value_unset (&profiles);
  g_free (device_name);
}

void
gst_mf_h264_enc_plugin_init (GstPlugin * plugin, guint rank)
{
  GstMFTransformEnumParams enum_params = { 0, };
  MFT_REGISTER_TYPE_INFO output_type;
  GstMFTransform *transform;
  gint i;
  gboolean do_next;

  GST_DEBUG_CATEGORY_INIT (gst_mf_h264_enc_debug, "mfh264enc", 0, "mfh264enc");

  output_type.guidMajorType = MFMediaType_Video;
  output_type.guidSubtype = MFVideoFormat_H264;

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
      gst_mf_h264_enc_plugin_init_internal (plugin, rank, transform,
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
      gst_mf_h264_enc_plugin_init_internal (plugin, rank, transform,
          enum_params.device_index, enum_params.enum_flags);
      gst_clear_object (&transform);
    }
  } while (do_next);
}
