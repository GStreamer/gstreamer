/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
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

#include "gstomxh263enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_h263_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_h263_enc_debug_category

/* prototypes */
static gboolean gst_omx_h263_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_h263_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h263_enc_debug_category, "omxh263enc", 0, \
      "debug category for gst-omx video encoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXH263Enc, gst_omx_h263_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_h263_enc_class_init (GstOMXH263EncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h263_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h263_enc_get_caps);

  videoenc_class->cdata.default_src_template_caps = "video/x-h263, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.263 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode H.263 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.h263");
}

static void
gst_omx_h263_enc_init (GstOMXH263Enc * self)
{
}

static gboolean
gst_omx_h263_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH263Enc *self = GST_OMX_H263_ENC (enc);
  GstCaps *peercaps;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;
  guint profile_id, level_id;

  gst_omx_port_get_port_definition (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingH263;
  err =
      gst_omx_port_update_port_definition (GST_OMX_VIDEO_ENC
      (self)->enc_out_port, &port_def);
  if (err != OMX_ErrorNone)
    return FALSE;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Getting profile/level not supported by component");
    return TRUE;
  }

  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));
  if (peercaps) {
    GstStructure *s;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);
    if (gst_structure_get_uint (s, "profile", &profile_id)) {
      switch (profile_id) {
        case 0:
          param.eProfile = OMX_VIDEO_H263ProfileBaseline;
          break;
        case 1:
          param.eProfile = OMX_VIDEO_H263ProfileH320Coding;
          break;
        case 2:
          param.eProfile = OMX_VIDEO_H263ProfileBackwardCompatible;
          break;
        case 3:
          param.eProfile = OMX_VIDEO_H263ProfileISWV2;
          break;
        case 4:
          param.eProfile = OMX_VIDEO_H263ProfileISWV3;
          break;
        case 5:
          param.eProfile = OMX_VIDEO_H263ProfileHighCompression;
          break;
        case 6:
          param.eProfile = OMX_VIDEO_H263ProfileInternet;
          break;
        case 7:
          param.eProfile = OMX_VIDEO_H263ProfileInterlace;
          break;
        case 8:
          param.eProfile = OMX_VIDEO_H263ProfileHighLatency;
          break;
        default:
          goto unsupported_profile;
      }
    }
    if (gst_structure_get_uint (s, "level", &level_id)) {
      switch (level_id) {
        case 10:
          param.eLevel = OMX_VIDEO_H263Level10;
          break;
        case 20:
          param.eLevel = OMX_VIDEO_H263Level20;
          break;
        case 30:
          param.eLevel = OMX_VIDEO_H263Level30;
          break;
        case 40:
          param.eLevel = OMX_VIDEO_H263Level40;
          break;
        case 50:
          param.eLevel = OMX_VIDEO_H263Level50;
          break;
        case 60:
          param.eLevel = OMX_VIDEO_H263Level60;
          break;
        case 70:
          param.eLevel = OMX_VIDEO_H263Level70;
          break;
        default:
          goto unsupported_level;
      }
    }
    gst_caps_unref (peercaps);
  }

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting profile/level not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting profile %u and level %u: %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (self, "Unsupported profile %u", profile_id);
  gst_caps_unref (peercaps);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %u", level_id);
  gst_caps_unref (peercaps);
  return FALSE;
}

static GstCaps *
gst_omx_h263_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH263Enc *self = GST_OMX_H263_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  guint profile, level;

  caps = gst_caps_new_empty_simple ("video/x-h263");

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone && err != OMX_ErrorUnsupportedIndex) {
    gst_caps_unref (caps);
    return NULL;
  }

  if (err == OMX_ErrorNone) {
    switch (param.eProfile) {
      case OMX_VIDEO_H263ProfileBaseline:
        profile = 0;
        break;
      case OMX_VIDEO_H263ProfileH320Coding:
        profile = 1;
        break;
      case OMX_VIDEO_H263ProfileBackwardCompatible:
        profile = 2;
        break;
      case OMX_VIDEO_H263ProfileISWV2:
        profile = 3;
        break;
      case OMX_VIDEO_H263ProfileISWV3:
        profile = 4;
        break;
      case OMX_VIDEO_H263ProfileHighCompression:
        profile = 5;
        break;
      case OMX_VIDEO_H263ProfileInternet:
        profile = 6;
        break;
      case OMX_VIDEO_H263ProfileInterlace:
        profile = 7;
        break;
      case OMX_VIDEO_H263ProfileHighLatency:
        profile = 8;
        break;
      default:
        g_assert_not_reached ();
        gst_caps_unref (caps);
        return NULL;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_H263Level10:
        level = 10;
        break;
      case OMX_VIDEO_H263Level20:
        level = 20;
        break;
      case OMX_VIDEO_H263Level30:
        level = 30;
        break;
      case OMX_VIDEO_H263Level40:
        level = 40;
        break;
      case OMX_VIDEO_H263Level50:
        level = 50;
        break;
      case OMX_VIDEO_H263Level60:
        level = 60;
        break;
      case OMX_VIDEO_H263Level70:
        level = 70;
        break;
      default:
        g_assert_not_reached ();
        gst_caps_unref (caps);
        return NULL;
    }

    gst_caps_set_simple (caps,
        "profile", G_TYPE_UINT, profile, "level", G_TYPE_UINT, level, NULL);
  }

  return caps;
}
