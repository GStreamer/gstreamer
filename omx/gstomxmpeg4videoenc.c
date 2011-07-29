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
static void gst_omx_mpeg4_video_enc_finalize (GObject * object);
static gboolean gst_omx_mpeg4_video_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);
static GstCaps *gst_omx_mpeg4_video_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mpeg4_video_enc_debug_category, "omxmpeg4videoenc", 0, \
      "debug category for gst-omx video encoder base class");

GST_BOILERPLATE_FULL (GstOMXMPEG4VideoEnc, gst_omx_mpeg4_video_enc,
    GstOMXVideoEnc, GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_mpeg4_video_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX MPEG4 Video Encoder",
      "Codec/Encoder/Video",
      "Encode MPEG4 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  /* If no role was set from the config file we set the
   * default MPEG4 video encoder role */
  if (!videoenc_class->component_role)
    videoenc_class->component_role = "video_encoder.mpeg4";
}

static void
gst_omx_mpeg4_video_enc_class_init (GstOMXMPEG4VideoEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  gobject_class->finalize = gst_omx_mpeg4_video_enc_finalize;

  videoenc_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_mpeg4_video_enc_set_format);
  videoenc_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_omx_mpeg4_video_enc_get_caps);

  videoenc_class->default_src_template_caps = "video/mpeg, "
      "mpegversion=(int) 4, "
      "systemstream=(boolean) false, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";
  videoenc_class->default_sink_template_caps = GST_VIDEO_CAPS_YUV ("I420");
}

static void
gst_omx_mpeg4_video_enc_init (GstOMXMPEG4VideoEnc * self,
    GstOMXMPEG4VideoEncClass * klass)
{
}

static void
gst_omx_mpeg4_video_enc_finalize (GObject * object)
{
  /* GstOMXMPEG4VideoEnc *self = GST_OMX_MPEG4_VIDEO_ENC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_omx_mpeg4_video_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstOMXMPEG4VideoEnc *self = GST_OMX_MPEG4_VIDEO_ENC (enc);
  GstCaps *peercaps;
  OMX_VIDEO_MPEG4PROFILETYPE profile = OMX_VIDEO_MPEG4ProfileSimple;
  OMX_VIDEO_MPEG4LEVELTYPE level = OMX_VIDEO_MPEG4Level1;

  peercaps = gst_pad_peer_get_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (enc));
  if (peercaps) {
    GstStructure *s;
    GstCaps *intersection;
    const gchar *profile_string, *level_string;
    OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
    OMX_ERRORTYPE err;

    intersection =
        gst_caps_intersect (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (enc)));
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
        profile = OMX_VIDEO_MPEG4ProfileSimple;
      } else if (g_str_equal (profile_string, "simple-scalable")) {
        profile = OMX_VIDEO_MPEG4ProfileSimpleScalable;
      } else if (g_str_equal (profile_string, "core")) {
        profile = OMX_VIDEO_MPEG4ProfileCore;
      } else if (g_str_equal (profile_string, "main")) {
        profile = OMX_VIDEO_MPEG4ProfileMain;
      } else if (g_str_equal (profile_string, "n-bit")) {
        profile = OMX_VIDEO_MPEG4ProfileNbit;
      } else if (g_str_equal (profile_string, "scalable")) {
        profile = OMX_VIDEO_MPEG4ProfileScalableTexture;
      } else if (g_str_equal (profile_string, "simple-face")) {
        profile = OMX_VIDEO_MPEG4ProfileSimpleFace;
      } else if (g_str_equal (profile_string, "simple-fba")) {
        profile = OMX_VIDEO_MPEG4ProfileSimpleFBA;
      } else if (g_str_equal (profile_string, "basic-animated-texture")) {
        profile = OMX_VIDEO_MPEG4ProfileBasicAnimated;
      } else if (g_str_equal (profile_string, "hybrid")) {
        profile = OMX_VIDEO_MPEG4ProfileHybrid;
      } else if (g_str_equal (profile_string, "advanced-real-time-simple")) {
        profile = OMX_VIDEO_MPEG4ProfileAdvancedRealTime;
      } else if (g_str_equal (profile_string, "core-scalable")) {
        profile = OMX_VIDEO_MPEG4ProfileCoreScalable;
      } else if (g_str_equal (profile_string, "advanced-coding-efficiency")) {
        profile = OMX_VIDEO_MPEG4ProfileAdvancedCoding;
      } else if (g_str_equal (profile_string, "advanced-core")) {
        profile = OMX_VIDEO_MPEG4ProfileAdvancedCore;
      } else if (g_str_equal (profile_string, "advanced-scalable-texture")) {
        profile = OMX_VIDEO_MPEG4ProfileAdvancedScalable;
      } else if (g_str_equal (profile_string, "advanced-simple")) {
        profile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
      } else {
        GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
        return FALSE;
      }
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      if (g_str_equal (level_string, "0")) {
        level = OMX_VIDEO_MPEG4Level0;
      } else if (g_str_equal (level_string, "0b")) {
        level = OMX_VIDEO_MPEG4Level0b;
      } else if (g_str_equal (level_string, "1")) {
        level = OMX_VIDEO_MPEG4Level1;
      } else if (g_str_equal (level_string, "2")) {
        level = OMX_VIDEO_MPEG4Level2;
      } else if (g_str_equal (level_string, "3")) {
        level = OMX_VIDEO_MPEG4Level3;
      } else if (g_str_equal (level_string, "4")) {
        level = OMX_VIDEO_MPEG4Level4;
      } else if (g_str_equal (level_string, "4a")) {
        level = OMX_VIDEO_MPEG4Level4a;
      } else if (g_str_equal (level_string, "5")) {
        level = OMX_VIDEO_MPEG4Level5;
      } else {
        GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
        return FALSE;
      }
    }

    GST_OMX_INIT_STRUCT (&param);
    param.nPortIndex = GST_OMX_VIDEO_ENC (self)->out_port->index;
    param.eProfile = profile;
    param.eLevel = level;

    err =
        gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->component,
        OMX_IndexParamVideoProfileLevelCurrent, &param);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "Error setting profile %d and level %d: %s (0x%08x)", profile, level,
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  }

  return TRUE;
}

static GstCaps *
gst_omx_mpeg4_video_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstOMXMPEG4VideoEnc *self = GST_OMX_MPEG4_VIDEO_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->component,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone)
    return NULL;

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
      break;
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
      break;
  }

  caps =
      gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE, "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level, NULL);

  if (state->fps_n != 0)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, state->fps_n,
        state->fps_d, NULL);
  if (state->par_n != 1 || state->par_d != 1)
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        state->par_n, state->par_d, NULL);

  return caps;
}
