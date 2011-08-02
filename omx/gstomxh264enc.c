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

#include "gstomxh264enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_h264_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_h264_enc_debug_category

/* prototypes */
static void gst_omx_h264_enc_finalize (GObject * object);
static gboolean gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);
static GstCaps *gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h264_enc_debug_category, "omxh264enc", 0, \
      "debug category for gst-omx video encoder base class");

GST_BOILERPLATE_FULL (GstOMXH264Enc, gst_omx_h264_enc,
    GstOMXVideoEnc, GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_h264_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX H.264 Video Encoder",
      "Codec/Encoder/Video",
      "Encode H.264 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  /* If no role was set from the config file we set the
   * default H264 video encoder role */
  if (!videoenc_class->component_role)
    videoenc_class->component_role = "video_encoder.avc";
}

static void
gst_omx_h264_enc_class_init (GstOMXH264EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  gobject_class->finalize = gst_omx_h264_enc_finalize;

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_get_caps);

  videoenc_class->default_src_template_caps = "video/x-h264, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";
  videoenc_class->default_sink_template_caps = GST_VIDEO_CAPS_YUV ("I420");
}

static void
gst_omx_h264_enc_init (GstOMXH264Enc * self, GstOMXH264EncClass * klass)
{
}

static void
gst_omx_h264_enc_finalize (GObject * object)
{
  /* GstOMXH264Enc *self = GST_OMX_H264_VIDEO_ENC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *peercaps;
  OMX_VIDEO_AVCPROFILETYPE profile = OMX_VIDEO_AVCProfileBaseline;
  OMX_VIDEO_AVCLEVELTYPE level = OMX_VIDEO_AVCLevel11;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;

  peercaps = gst_pad_peer_get_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (enc));
  if (peercaps) {
    GstStructure *s;
    GstCaps *intersection;
    const gchar *profile_string, *level_string;

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
      if (g_str_equal (profile_string, "baseline")) {
        profile = OMX_VIDEO_AVCProfileBaseline;
      } else if (g_str_equal (profile_string, "main")) {
        profile = OMX_VIDEO_AVCProfileMain;
      } else if (g_str_equal (profile_string, "extended")) {
        profile = OMX_VIDEO_AVCProfileExtended;
      } else if (g_str_equal (profile_string, "high")) {
        profile = OMX_VIDEO_AVCProfileHigh;
      } else if (g_str_equal (profile_string, "high-10")) {
        profile = OMX_VIDEO_AVCProfileHigh10;
      } else if (g_str_equal (profile_string, "high-4:2:2")) {
        profile = OMX_VIDEO_AVCProfileHigh422;
      } else if (g_str_equal (profile_string, "high-4:4:4")) {
        profile = OMX_VIDEO_AVCProfileHigh444;
      } else {
        GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
        return FALSE;
      }
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      if (g_str_equal (level_string, "1")) {
        level = OMX_VIDEO_AVCLevel1;
      } else if (g_str_equal (level_string, "1b")) {
        level = OMX_VIDEO_AVCLevel1b;
      } else if (g_str_equal (level_string, "1.1")) {
        level = OMX_VIDEO_AVCLevel11;
      } else if (g_str_equal (level_string, "1.2")) {
        level = OMX_VIDEO_AVCLevel12;
      } else if (g_str_equal (level_string, "1.3")) {
        level = OMX_VIDEO_AVCLevel13;
      } else if (g_str_equal (level_string, "2")) {
        level = OMX_VIDEO_AVCLevel2;
      } else if (g_str_equal (level_string, "2.1")) {
        level = OMX_VIDEO_AVCLevel21;
      } else if (g_str_equal (level_string, "2.2")) {
        level = OMX_VIDEO_AVCLevel22;
      } else if (g_str_equal (level_string, "3")) {
        level = OMX_VIDEO_AVCLevel3;
      } else if (g_str_equal (level_string, "3.1")) {
        level = OMX_VIDEO_AVCLevel31;
      } else if (g_str_equal (level_string, "3.2")) {
        level = OMX_VIDEO_AVCLevel32;
      } else if (g_str_equal (level_string, "4")) {
        level = OMX_VIDEO_AVCLevel4;
      } else if (g_str_equal (level_string, "4.1")) {
        level = OMX_VIDEO_AVCLevel41;
      } else if (g_str_equal (level_string, "4.2")) {
        level = OMX_VIDEO_AVCLevel42;
      } else if (g_str_equal (level_string, "5")) {
        level = OMX_VIDEO_AVCLevel5;
      } else if (g_str_equal (level_string, "5.1")) {
        level = OMX_VIDEO_AVCLevel51;
      } else {
        GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
        return FALSE;
      }
    }
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->out_port->index;
  param.eProfile = profile;
  param.eLevel = level;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->component,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting profile/level not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting profile %d and level %d: %s (0x%08x)", profile, level,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level;

  caps =
      gst_caps_new_simple ("video/x-h264", "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height, NULL);

  if (state->fps_n != 0)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, state->fps_n,
        state->fps_d, NULL);
  if (state->par_n != 1 || state->par_d != 1)
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        state->par_n, state->par_d, NULL);

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->component,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone && err != OMX_ErrorUnsupportedIndex)
    return NULL;

  if (err == OMX_ErrorNone) {
    switch (param.eProfile) {
      case OMX_VIDEO_AVCProfileBaseline:
        profile = "baseline";
        break;
      case OMX_VIDEO_AVCProfileMain:
        profile = "main";
        break;
      case OMX_VIDEO_AVCProfileExtended:
        profile = "extended";
        break;
      case OMX_VIDEO_AVCProfileHigh:
        profile = "high";
        break;
      case OMX_VIDEO_AVCProfileHigh10:
        profile = "high-10";
        break;
      case OMX_VIDEO_AVCProfileHigh422:
        profile = "high-4:2:2";
        break;
      case OMX_VIDEO_AVCProfileHigh444:
        profile = "high-4:4:4";
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_AVCLevel1:
        level = "1";
        break;
      case OMX_VIDEO_AVCLevel1b:
        level = "1b";
        break;
      case OMX_VIDEO_AVCLevel11:
        level = "1.1";
        break;
      case OMX_VIDEO_AVCLevel12:
        level = "1.2";
        break;
      case OMX_VIDEO_AVCLevel13:
        level = "1.3";
        break;
      case OMX_VIDEO_AVCLevel2:
        level = "2";
        break;
      case OMX_VIDEO_AVCLevel21:
        level = "2.1";
        break;
      case OMX_VIDEO_AVCLevel22:
        level = "2.2";
        break;
      case OMX_VIDEO_AVCLevel3:
        level = "3";
        break;
      case OMX_VIDEO_AVCLevel31:
        level = "3.1";
        break;
      case OMX_VIDEO_AVCLevel32:
        level = "3.2";
        break;
      case OMX_VIDEO_AVCLevel4:
        level = "4";
        break;
      case OMX_VIDEO_AVCLevel41:
        level = "4.1";
        break;
      case OMX_VIDEO_AVCLevel42:
        level = "4.2";
        break;
      case OMX_VIDEO_AVCLevel5:
        level = "5";
        break;
      case OMX_VIDEO_AVCLevel51:
        level = "5.1";
        break;
      default:
        g_assert_not_reached ();
        break;
    }
    gst_caps_set_simple (caps,
        "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level, NULL);
  }

  return caps;
}
