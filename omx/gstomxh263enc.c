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
static void gst_omx_h263_enc_finalize (GObject * object);
static gboolean gst_omx_h263_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);
static GstCaps *gst_omx_h263_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h263_enc_debug_category, "omxh263enc", 0, \
      "debug category for gst-omx video encoder base class");

GST_BOILERPLATE_FULL (GstOMXH263Enc, gst_omx_h263_enc,
    GstOMXVideoEnc, GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_h263_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX H.263 Video Encoder",
      "Codec/Encoder/Video",
      "Encode H.263 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  /* If no role was set from the config file we set the
   * default H263 video encoder role */
  if (!videoenc_class->component_role)
    videoenc_class->component_role = "video_encoder.h263";
}

static void
gst_omx_h263_enc_class_init (GstOMXH263EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  gobject_class->finalize = gst_omx_h263_enc_finalize;

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h263_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h263_enc_get_caps);

  videoenc_class->default_src_template_caps = "video/x-h263, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";
  videoenc_class->default_sink_template_caps = GST_VIDEO_CAPS_YUV ("I420");
}

static void
gst_omx_h263_enc_init (GstOMXH263Enc * self, GstOMXH263EncClass * klass)
{
}

static void
gst_omx_h263_enc_finalize (GObject * object)
{
  /* GstOMXH263Enc *self = GST_OMX_H263_VIDEO_ENC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_omx_h263_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstOMXH263Enc *self = GST_OMX_H263_ENC (enc);
  GstCaps *peercaps;
  OMX_VIDEO_H263PROFILETYPE profile = OMX_VIDEO_H263ProfileBaseline;
  OMX_VIDEO_H263LEVELTYPE level = OMX_VIDEO_H263Level10;

  peercaps = gst_pad_peer_get_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (enc));
  if (peercaps) {
    GstStructure *s;
    GstCaps *intersection;
    guint profile_id, level_id;
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
    if (gst_structure_get_uint (s, "profile", &profile_id)) {
      switch (profile_id) {
        case 0:
          profile = OMX_VIDEO_H263ProfileBaseline;
          break;
        case 1:
          profile = OMX_VIDEO_H263ProfileH320Coding;
          break;
        case 2:
          profile = OMX_VIDEO_H263ProfileBackwardCompatible;
          break;
        case 3:
          profile = OMX_VIDEO_H263ProfileISWV2;
          break;
        case 4:
          profile = OMX_VIDEO_H263ProfileISWV3;
          break;
        case 5:
          profile = OMX_VIDEO_H263ProfileHighCompression;
          break;
        case 6:
          profile = OMX_VIDEO_H263ProfileInternet;
          break;
        case 7:
          profile = OMX_VIDEO_H263ProfileInterlace;
          break;
        case 8:
          profile = OMX_VIDEO_H263ProfileHighLatency;
          break;
        default:
          GST_ERROR_OBJECT (self, "Invalid profile %u", profile_id);
          return FALSE;
      }
    }
    if (gst_structure_get_uint (s, "level", &level_id)) {
      switch (level_id) {
        case 10:
          level = OMX_VIDEO_H263Level10;
          break;
        case 20:
          level = OMX_VIDEO_H263Level20;
          break;
        case 30:
          level = OMX_VIDEO_H263Level30;
          break;
        case 40:
          level = OMX_VIDEO_H263Level40;
          break;
        case 50:
          level = OMX_VIDEO_H263Level50;
          break;
        case 60:
          level = OMX_VIDEO_H263Level60;
          break;
        case 70:
          level = OMX_VIDEO_H263Level70;
          break;
        default:
          GST_ERROR_OBJECT (self, "Unsupported level %u", level_id);
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
gst_omx_h263_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoState * state)
{
  GstOMXH263Enc *self = GST_OMX_H263_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  guint profile, level;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->component,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone)
    return NULL;

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
      break;
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
      break;
  }

  caps =
      gst_caps_new_simple ("video/x-h263", "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "profile", G_TYPE_UINT, profile, "level", G_TYPE_UINT, level, NULL);

  if (state->fps_n != 0)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, state->fps_n,
        state->fps_d, NULL);
  if (state->par_n != 1 || state->par_d != 1)
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        state->par_n, state->par_d, NULL);

  return caps;
}
