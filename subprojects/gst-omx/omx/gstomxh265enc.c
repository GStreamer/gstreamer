/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2017 Xilinx, Inc.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxh265enc.h"
#include "gstomxh265utils.h"
#include "gstomxvideo.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_h265_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_h265_enc_debug_category

/* prototypes */
static gboolean gst_omx_h265_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_h265_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static void gst_omx_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_omx_h265_enc_handle_output_frame (GstOMXVideoEnc *
    self, GstOMXPort * port, GstOMXBuffer * buf, GstVideoCodecFrame * frame);

enum
{
  PROP_0,
  PROP_PERIODICITYOFIDRFRAMES,
  PROP_INTERVALOFCODINGINTRAFRAMES,
  PROP_B_FRAMES,
  PROP_CONSTRAINED_INTRA_PREDICTION,
  PROP_LOOP_FILTER_MODE,
};

#define GST_OMX_H265_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT    (0xffffffff)
#define GST_OMX_H265_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_H265_VIDEO_ENC_B_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_H265_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT (FALSE)
#define GST_OMX_H265_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT (0xffffffff)

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
/* zynqultrascaleplus's OMX uses a param struct different of Android's one */
#define INDEX_PARAM_VIDEO_HEVC OMX_ALG_IndexParamVideoHevc
#define ALIGNMENT "{ au, nal }"
#else
#define INDEX_PARAM_VIDEO_HEVC OMX_IndexParamVideoHevc
#define ALIGNMENT "au"
#endif

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h265_enc_debug_category, "omxh265enc", 0, \
      "debug category for gst-omx H265 video encoder");

#define parent_class gst_omx_h265_enc_parent_class
G_DEFINE_TYPE_WITH_CODE (GstOMXH265Enc, gst_omx_h265_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
#define GST_TYPE_OMX_H265_ENC_LOOP_FILTER_MODE (gst_omx_h265_enc_loop_filter_mode_get_type ())
static GType
gst_omx_h265_enc_loop_filter_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_ALG_VIDEO_HEVCLoopFilterEnable, "Enable deblocking filter",
          "enable"},
      {OMX_ALG_VIDEO_HEVCLoopFilterDisable, "Disable deblocking filter",
          "disable"},
      {OMX_ALG_VIDEO_HEVCLoopFilterDisableCrossSlice,
          "Disable deblocking filter on slice boundary", "disable-cross-slice"},
      {OMX_ALG_VIDEO_HEVCLoopFilterDisableCrossTile,
          "Disable deblocking filter on tile boundary", "disable-cross-tile"},
      {OMX_ALG_VIDEO_HEVCLoopFilterDisableCrossSliceAndTile,
            "Disable deblocking filter on slice and tile boundary",
          "disable-slice-and-tile"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXH265EncLoopFilter", values);
  }
  return qtype;
}
#endif

static gboolean
gst_omx_h265_enc_flush (GstVideoEncoder * enc)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (enc);

  g_list_free_full (self->headers, (GDestroyNotify) gst_buffer_unref);
  self->headers = NULL;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (enc);
}

static gboolean
gst_omx_h265_enc_stop (GstVideoEncoder * enc)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (enc);

  g_list_free_full (self->headers, (GDestroyNotify) gst_buffer_unref);
  self->headers = NULL;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (enc);
}

static void
gst_omx_h265_enc_class_init (GstOMXH265EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *basevideoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h265_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h265_enc_get_caps);
  videoenc_class->handle_output_frame =
      GST_DEBUG_FUNCPTR (gst_omx_h265_enc_handle_output_frame);

  basevideoenc_class->flush = gst_omx_h265_enc_flush;
  basevideoenc_class->stop = gst_omx_h265_enc_stop;

  gobject_class->set_property = gst_omx_h265_enc_set_property;
  gobject_class->get_property = gst_omx_h265_enc_get_property;

  g_object_class_install_property (gobject_class,
      PROP_INTERVALOFCODINGINTRAFRAMES,
      g_param_spec_uint ("interval-intraframes",
          "Interval of coding Intra frames",
          "Interval of coding Intra frames (0xffffffff=component default)", 0,
          G_MAXUINT,
          GST_OMX_H265_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  g_object_class_install_property (gobject_class, PROP_PERIODICITYOFIDRFRAMES,
      g_param_spec_uint ("periodicity-idr", "IDR periodicity",
          "Periodicity of IDR frames (0xffffffff=component default)",
          0, G_MAXUINT,
          GST_OMX_H265_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_B_FRAMES,
      g_param_spec_uint ("b-frames", "Number of B-frames",
          "Number of B-frames between two consecutive I-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_H265_VIDEO_ENC_B_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_CONSTRAINED_INTRA_PREDICTION,
      g_param_spec_boolean ("constrained-intra-prediction",
          "Constrained Intra Prediction",
          "If enabled, prediction only uses residual data and decoded samples "
          "from neighbouring coding blocks coded using intra prediction modes",
          GST_OMX_H265_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LOOP_FILTER_MODE,
      g_param_spec_enum ("loop-filter-mode", "Loop Filter mode",
          "Enable or disable the deblocking filter (0xffffffff=component default)",
          GST_TYPE_OMX_H265_ENC_LOOP_FILTER_MODE,
          GST_OMX_H265_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  videoenc_class->cdata.default_sink_template_caps =
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_FORMAT_INTERLACED,
      GST_OMX_VIDEO_ENC_SUPPORTED_FORMATS)
      ", interlace-mode = (string) alternate ; "
#endif
      GST_VIDEO_CAPS_MAKE (GST_OMX_VIDEO_ENC_SUPPORTED_FORMATS);

  videoenc_class->cdata.default_src_template_caps = "video/x-h265, "
      "width=(int) [ 1, MAX ], " "height=(int) [ 1, MAX ], "
      "framerate = (fraction) [0, MAX], stream-format=(string) byte-stream, "
      "aligmment = (string) " ALIGNMENT;

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.265 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode H.265 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.hevc");
}

static void
gst_omx_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (object);

  switch (prop_id) {
    case PROP_INTERVALOFCODINGINTRAFRAMES:
      self->interval_intraframes = g_value_get_uint (value);
      break;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    case PROP_PERIODICITYOFIDRFRAMES:
      self->periodicity_idr = g_value_get_uint (value);
      break;
    case PROP_B_FRAMES:
      self->b_frames = g_value_get_uint (value);
      break;
    case PROP_CONSTRAINED_INTRA_PREDICTION:
      self->constrained_intra_prediction = g_value_get_boolean (value);
      break;
    case PROP_LOOP_FILTER_MODE:
      self->loop_filter_mode = g_value_get_enum (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h265_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (object);

  switch (prop_id) {
    case PROP_INTERVALOFCODINGINTRAFRAMES:
      g_value_set_uint (value, self->interval_intraframes);
      break;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    case PROP_PERIODICITYOFIDRFRAMES:
      g_value_set_uint (value, self->periodicity_idr);
      break;
    case PROP_B_FRAMES:
      g_value_set_uint (value, self->b_frames);
      break;
    case PROP_CONSTRAINED_INTRA_PREDICTION:
      g_value_set_boolean (value, self->constrained_intra_prediction);
      break;
    case PROP_LOOP_FILTER_MODE:
      g_value_set_enum (value, self->loop_filter_mode);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h265_enc_init (GstOMXH265Enc * self)
{
  self->interval_intraframes =
      GST_OMX_H265_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  self->periodicity_idr =
      GST_OMX_H265_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT;
  self->b_frames = GST_OMX_H265_VIDEO_ENC_B_FRAMES_DEFAULT;
  self->constrained_intra_prediction =
      GST_OMX_H265_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT;
  self->loop_filter_mode = GST_OMX_H265_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT;
#endif
}

/* Update OMX_VIDEO_PARAM_PROFILELEVELTYPE.{eProfile,eLevel}
 *
 * Returns TRUE if succeeded or if not supported, FALSE if failed */
static gboolean
update_param_profile_level (GstOMXH265Enc * self,
    OMX_VIDEO_HEVCPROFILETYPE profile, OMX_VIDEO_HEVCLEVELTYPE level)
{
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Getting OMX_IndexParamVideoProfileLevelCurrent not supported by component");
    return TRUE;
  }

  if (profile != OMX_VIDEO_HEVCProfileUnknown)
    param.eProfile = profile;
  if (level != OMX_VIDEO_HEVCLevelUnknown)
    param.eLevel = level;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting OMX_IndexParamVideoProfileLevelCurrent not supported by component");
    return TRUE;
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting profile %u and level %u: %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

/* Update OMX_ALG_VIDEO_PARAM_HEVCTYPE
 *
 * Returns TRUE if succeeded or if not supported, FALSE if failed */
static gboolean
update_param_hevc (GstOMXH265Enc * self,
    OMX_VIDEO_HEVCPROFILETYPE profile, OMX_VIDEO_HEVCLEVELTYPE level)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_ALG_VIDEO_PARAM_HEVCTYPE param;
#else
  OMX_VIDEO_PARAM_HEVCTYPE param;
#endif
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  /* On Android the param struct is initialized manually with default
   * settings rather than using GetParameter() to retrieve them.
   * We should probably do the same when we'll add Android as target.
   * See bgo#783862 for details. */

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  param.bConstIpred = self->constrained_intra_prediction;

  if (self->loop_filter_mode != GST_OMX_H265_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT)
    param.eLoopFilterMode = self->loop_filter_mode;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoHevc, &param);
#else
  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_IndexParamVideoHevc, &param);
#endif

  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Getting OMX_ALG_IndexParamVideoHevc not supported by component");
    return TRUE;
  }

  if (profile != OMX_VIDEO_HEVCProfileUnknown)
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    param.eProfile = (OMX_ALG_VIDEO_HEVCPROFILETYPE) profile;
#else
    param.eProfile = profile;
#endif

  if (level != OMX_VIDEO_HEVCLevelUnknown)
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
    param.eLevel = (OMX_ALG_VIDEO_HEVCLEVELTYPE) level;
#else
    param.eLevel = level;
#endif

  /* GOP pattern */
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  /* The zynqultrascaleplus uses another PARAM_HEVCTYPE API allowing users to
   * define the number of P and B frames while Android's API only expose the
   * former. */
  if (self->interval_intraframes !=
      GST_OMX_H265_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT) {
    param.nPFrames = self->interval_intraframes;

    /* If user specified a specific number of B-frames, reduce the number of
     * P-frames by this amount. If not ensure there is no B-frame to have the
     * requested GOP length. */
    if (self->b_frames != GST_OMX_H265_VIDEO_ENC_B_FRAMES_DEFAULT) {
      if (self->b_frames > self->interval_intraframes) {
        GST_ERROR_OBJECT (self,
            "The interval_intraframes perdiod (%u) needs to be higher than the number of B-frames (%u)",
            self->interval_intraframes, self->b_frames);
        return FALSE;
      }
      param.nPFrames -= self->b_frames;
    } else {
      param.nBFrames = 0;
    }
  }

  if (self->b_frames != GST_OMX_H265_VIDEO_ENC_B_FRAMES_DEFAULT)
    param.nBFrames = self->b_frames;
#else
  if (self->interval_intraframes !=
      GST_OMX_H265_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT)
    param.nKeyFrameInterval = self->interval_intraframes;
#endif

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) INDEX_PARAM_VIDEO_HEVC, &param);

  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting IndexParamVideoHevc not supported by component");
    return TRUE;
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting HEVC settings (profile %u and level %u): %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
static gboolean
set_intra_period (GstOMXH265Enc * self)
{
  OMX_ALG_VIDEO_PARAM_INSTANTANEOUS_DECODING_REFRESH config_idr;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&config_idr);
  config_idr.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  GST_DEBUG_OBJECT (self, "nIDRPeriod:%u",
      (guint) config_idr.nInstantaneousDecodingRefreshFrequency);

  config_idr.nInstantaneousDecodingRefreshFrequency = self->periodicity_idr;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoInstantaneousDecodingRefresh,
      &config_idr);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_IndexConfigVideoAVCIntraPeriod %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}
#endif

static gboolean
gst_omx_h265_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (enc);
  GstCaps *peercaps;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_ERRORTYPE err;
  const gchar *profile_string, *level_string, *tier_string;
  OMX_VIDEO_HEVCPROFILETYPE profile = OMX_VIDEO_HEVCProfileUnknown;
  OMX_VIDEO_HEVCLEVELTYPE level = OMX_VIDEO_HEVCLevelUnknown;
  gboolean enable_subframe = FALSE;

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  if (self->periodicity_idr !=
      GST_OMX_H265_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT)
    set_intra_period (self);
#endif

  gst_omx_port_get_port_definition (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      &port_def);
  port_def.format.video.eCompressionFormat =
      (OMX_VIDEO_CODINGTYPE) OMX_VIDEO_CodingHEVC;
  err =
      gst_omx_port_update_port_definition (GST_OMX_VIDEO_ENC
      (self)->enc_out_port, &port_def);
  if (err != OMX_ErrorNone)
    return FALSE;

  /* Set profile and level */
  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));
  if (peercaps) {
    GstStructure *s;
    const gchar *alignment_string;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      profile = gst_omx_h265_utils_get_profile_from_str (profile_string);
      if (profile == OMX_VIDEO_HEVCProfileUnknown)
        goto unsupported_profile;
    }

    level_string = gst_structure_get_string (s, "level");
    tier_string = gst_structure_get_string (s, "tier");
    if (level_string && tier_string) {
      level = gst_omx_h265_utils_get_level_from_str (level_string, tier_string);
      if (level == OMX_VIDEO_HEVCLevelUnknown)
        goto unsupported_level;
    }

    alignment_string = gst_structure_get_string (s, "alignment");
    if (alignment_string && g_str_equal (alignment_string, "nal"))
      enable_subframe = TRUE;

    gst_caps_unref (peercaps);
  }

  if (profile != OMX_VIDEO_HEVCProfileUnknown
      || level != OMX_VIDEO_HEVCLevelUnknown) {
    /* OMX provides 2 API to set the profile and level. We try using the
     * generic one here and the H265 specific when calling
     * update_param_hevc() */
    if (!update_param_profile_level (self, profile, level))
      return FALSE;
  }

  if (!update_param_hevc (self, profile, level))
    return FALSE;

  gst_omx_port_set_subframe (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      enable_subframe);

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
  gst_caps_unref (peercaps);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
  gst_caps_unref (peercaps);
  return FALSE;
}

static GstCaps *
gst_omx_h265_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level, *tier, *alignment;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone && err != OMX_ErrorUnsupportedIndex)
    return NULL;

  if (gst_omx_port_get_subframe (GST_OMX_VIDEO_ENC (self)->enc_out_port))
    alignment = "nal";
  else
    alignment = "au";

  caps = gst_caps_new_simple ("video/x-h265",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, alignment, NULL);

  if (err == OMX_ErrorNone) {
    profile = gst_omx_h265_utils_get_profile_from_enum (param.eProfile);
    if (!profile) {
      g_assert_not_reached ();
      gst_caps_unref (caps);
      return NULL;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_HEVCMainTierLevel1:
        tier = "main";
        level = "1";
        break;
      case OMX_VIDEO_HEVCMainTierLevel2:
        tier = "main";
        level = "2";
        break;
      case OMX_VIDEO_HEVCMainTierLevel21:
        tier = "main";
        level = "2.1";
        break;
      case OMX_VIDEO_HEVCMainTierLevel3:
        tier = "main";
        level = "3";
        break;
      case OMX_VIDEO_HEVCMainTierLevel31:
        tier = "main";
        level = "3.1";
        break;
      case OMX_VIDEO_HEVCMainTierLevel4:
        tier = "main";
        level = "4";
        break;
      case OMX_VIDEO_HEVCMainTierLevel41:
        tier = "main";
        level = "4.1";
        break;
      case OMX_VIDEO_HEVCMainTierLevel5:
        tier = "main";
        level = "5";
        break;
      case OMX_VIDEO_HEVCMainTierLevel51:
        tier = "main";
        level = "5.1";
        break;
      case OMX_VIDEO_HEVCMainTierLevel52:
        tier = "main";
        level = "5.2";
        break;
      case OMX_VIDEO_HEVCMainTierLevel6:
        tier = "main";
        level = "6";
        break;
      case OMX_VIDEO_HEVCMainTierLevel61:
        tier = "main";
        level = "6.1";
        break;
      case OMX_VIDEO_HEVCMainTierLevel62:
        tier = "main";
        level = "6.2";
        break;
      case OMX_VIDEO_HEVCHighTierLevel4:
        tier = "high";
        level = "4";
        break;
      case OMX_VIDEO_HEVCHighTierLevel41:
        tier = "high";
        level = "4.1";
        break;
      case OMX_VIDEO_HEVCHighTierLevel5:
        tier = "high";
        level = "5";
        break;
      case OMX_VIDEO_HEVCHighTierLevel51:
        tier = "high";
        level = "5.1";
        break;
      case OMX_VIDEO_HEVCHighTierLevel52:
        tier = "high";
        level = "5.2";
        break;
      case OMX_VIDEO_HEVCHighTierLevel6:
        tier = "high";
        level = "6";
        break;
      case OMX_VIDEO_HEVCHighTierLevel61:
        tier = "high";
        level = "6.1";
        break;
      case OMX_VIDEO_HEVCHighTierLevel62:
        tier = "high";
        level = "6.2";
        break;
      default:
        g_assert_not_reached ();
        gst_caps_unref (caps);
        return NULL;
    }

    gst_caps_set_simple (caps,
        "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level,
        "tier", G_TYPE_STRING, tier, NULL);
  }

  return caps;
}

static GstFlowReturn
gst_omx_h265_enc_handle_output_frame (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstOMXBuffer * buf, GstVideoCodecFrame * frame)
{
  GstOMXH265Enc *self = GST_OMX_H265_ENC (enc);

  if (buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
    /* The codec data is SPS/PPS but our output is stream-format=byte-stream.
     * For bytestream stream format the SPS/PPS is only in-stream and not
     * in the caps!
     */
    GstBuffer *hdrs;
    GstMapInfo map = GST_MAP_INFO_INIT;
    GstFlowReturn flow_ret;

    GST_DEBUG_OBJECT (self, "got codecconfig in byte-stream format");

    hdrs = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);
    GST_BUFFER_FLAG_SET (hdrs, GST_BUFFER_FLAG_HEADER);

    gst_buffer_map (hdrs, &map, GST_MAP_WRITE);
    memcpy (map.data,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);
    gst_buffer_unmap (hdrs, &map);
    self->headers = g_list_append (self->headers, gst_buffer_ref (hdrs));
    frame->output_buffer = hdrs;
    flow_ret =
        gst_video_encoder_finish_subframe (GST_VIDEO_ENCODER (self), frame);
    gst_video_codec_frame_unref (frame);

    return flow_ret;
  } else if (self->headers) {
    gst_video_encoder_set_headers (GST_VIDEO_ENCODER (self), self->headers);
    self->headers = NULL;
  }

  return
      GST_OMX_VIDEO_ENC_CLASS
      (gst_omx_h265_enc_parent_class)->handle_output_frame (enc, port, buf,
      frame);
}
