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

#include "gstomxmpeg4videoenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mpeg4_video_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_mpeg4_video_enc_debug_category

/* prototypes */
static gboolean gst_omx_mpeg4_video_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_mpeg4_video_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mpeg4_video_enc_debug_category, "omxmpeg4videoenc", 0, \
      "debug category for gst-omx video encoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXMPEG4VideoEnc, gst_omx_mpeg4_video_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_mpeg4_video_enc_class_init (GstOMXMPEG4VideoEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  videoenc_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_mpeg4_video_enc_set_format);
  videoenc_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_omx_mpeg4_video_enc_get_caps);

  videoenc_class->cdata.default_src_template_caps = "video/mpeg, "
      "mpegversion=(int) 4, "
      "systemstream=(boolean) false, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX MPEG4 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode MPEG4 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.mpeg4");
}

static void
gst_omx_mpeg4_video_enc_init (GstOMXMPEG4VideoEnc * self)
{
}

static gboolean
gst_omx_mpeg4_video_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXMPEG4VideoEnc *self = GST_OMX_MPEG4_VIDEO_ENC (enc);
  GstCaps *peercaps, *intersection;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;
  const gchar *profile_string, *level_string;

  gst_omx_port_get_port_definition (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
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
    return FALSE;
  }

  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc), NULL);
  if (peercaps) {
    GstStructure *s;

    intersection =
        gst_caps_intersect (peercaps,
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));

    gst_caps_unref (peercaps);
    if (gst_caps_is_empty (intersection)) {
      gst_caps_unref (intersection);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (intersection, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      if (g_str_equal (profile_string, "simple")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
      } else if (g_str_equal (profile_string, "simple-scalable")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileSimpleScalable;
      } else if (g_str_equal (profile_string, "core")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileCore;
      } else if (g_str_equal (profile_string, "main")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileMain;
      } else if (g_str_equal (profile_string, "n-bit")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileNbit;
      } else if (g_str_equal (profile_string, "scalable")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileScalableTexture;
      } else if (g_str_equal (profile_string, "simple-face")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileSimpleFace;
      } else if (g_str_equal (profile_string, "simple-fba")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileSimpleFBA;
      } else if (g_str_equal (profile_string, "basic-animated-texture")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileBasicAnimated;
      } else if (g_str_equal (profile_string, "hybrid")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileHybrid;
      } else if (g_str_equal (profile_string, "advanced-real-time-simple")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileAdvancedRealTime;
      } else if (g_str_equal (profile_string, "core-scalable")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileCoreScalable;
      } else if (g_str_equal (profile_string, "advanced-coding-efficiency")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileAdvancedCoding;
      } else if (g_str_equal (profile_string, "advanced-core")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileAdvancedCore;
      } else if (g_str_equal (profile_string, "advanced-scalable-texture")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileAdvancedScalable;
      } else if (g_str_equal (profile_string, "advanced-simple")) {
        param.eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
      } else {
        goto unsupported_profile;
      }
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      if (g_str_equal (level_string, "0")) {
        param.eLevel = OMX_VIDEO_MPEG4Level0;
      } else if (g_str_equal (level_string, "0b")) {
        param.eLevel = OMX_VIDEO_MPEG4Level0b;
      } else if (g_str_equal (level_string, "1")) {
        param.eLevel = OMX_VIDEO_MPEG4Level1;
      } else if (g_str_equal (level_string, "2")) {
        param.eLevel = OMX_VIDEO_MPEG4Level2;
      } else if (g_str_equal (level_string, "3")) {
        param.eLevel = OMX_VIDEO_MPEG4Level3;
      } else if (g_str_equal (level_string, "4")) {
        param.eLevel = OMX_VIDEO_MPEG4Level4;
      } else if (g_str_equal (level_string, "4a")) {
        param.eLevel = OMX_VIDEO_MPEG4Level4a;
      } else if (g_str_equal (level_string, "5")) {
        param.eLevel = OMX_VIDEO_MPEG4Level5;
      } else {
        goto unsupported_level;
      }
    }

    gst_caps_unref (intersection);
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
  GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
  gst_caps_unref (intersection);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
  gst_caps_unref (intersection);
  return FALSE;
}

static GstCaps *
gst_omx_mpeg4_video_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXMPEG4VideoEnc *self = GST_OMX_MPEG4_VIDEO_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level;

  caps =
      gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);

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
      case OMX_VIDEO_MPEG4ProfileSimple:
        profile = "simple";
        break;
      case OMX_VIDEO_MPEG4ProfileSimpleScalable:
        profile = "simple-scalable";
        break;
      case OMX_VIDEO_MPEG4ProfileCore:
        profile = "core";
        break;
      case OMX_VIDEO_MPEG4ProfileMain:
        profile = "main";
        break;
      case OMX_VIDEO_MPEG4ProfileNbit:
        profile = "n-bit";
        break;
      case OMX_VIDEO_MPEG4ProfileScalableTexture:
        profile = "scalable";
        break;
      case OMX_VIDEO_MPEG4ProfileSimpleFace:
        profile = "simple-face";
        break;
      case OMX_VIDEO_MPEG4ProfileSimpleFBA:
        profile = "simple-fba";
        break;
      case OMX_VIDEO_MPEG4ProfileBasicAnimated:
        profile = "basic-animated-texture";
        break;
      case OMX_VIDEO_MPEG4ProfileHybrid:
        profile = "hybrid";
        break;
      case OMX_VIDEO_MPEG4ProfileAdvancedRealTime:
        profile = "advanced-real-time-simple";
        break;
      case OMX_VIDEO_MPEG4ProfileCoreScalable:
        profile = "core-scalable";
        break;
      case OMX_VIDEO_MPEG4ProfileAdvancedCoding:
        profile = "advanced-coding-efficiency";
        break;
      case OMX_VIDEO_MPEG4ProfileAdvancedCore:
        profile = "advanced-core";
        break;
      case OMX_VIDEO_MPEG4ProfileAdvancedScalable:
        profile = "advanced-scalable-texture";
        break;
      case OMX_VIDEO_MPEG4ProfileAdvancedSimple:
        profile = "advanced-simple";
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_MPEG4Level0:
        level = "0";
        break;
      case OMX_VIDEO_MPEG4Level0b:
        level = "0b";
        break;
      case OMX_VIDEO_MPEG4Level1:
        level = "1";
        break;
      case OMX_VIDEO_MPEG4Level2:
        level = "2";
        break;
      case OMX_VIDEO_MPEG4Level3:
        level = "3";
        break;
      case OMX_VIDEO_MPEG4Level4:
        level = "4";
        break;
      case OMX_VIDEO_MPEG4Level4a:
        level = "4a";
        break;
      case OMX_VIDEO_MPEG4Level5:
        level = "5";
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }

    gst_caps_set_simple (caps,
        "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level, NULL);
  }

  return caps;
}
