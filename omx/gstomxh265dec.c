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

#include "gstomxh265dec.h"
#include "gstomxh265utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_h265_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_h265_dec_debug_category

/* prototypes */
static gboolean gst_omx_h265_dec_is_format_change (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state);
static gboolean gst_omx_h265_dec_set_format (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h265_dec_debug_category, "omxh265dec", 0, \
      "debug category for gst-omx H265 video decoder");

G_DEFINE_TYPE_WITH_CODE (GstOMXH265Dec, gst_omx_h265_dec,
    GST_TYPE_OMX_VIDEO_DEC, DEBUG_INIT);

static void
gst_omx_h265_dec_class_init (GstOMXH265DecClass * klass)
{
  GstOMXVideoDecClass *videodec_class = GST_OMX_VIDEO_DEC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  videodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_h265_dec_is_format_change);
  videodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h265_dec_set_format);

  videodec_class->cdata.default_sink_template_caps = "video/x-h265, "
      "alignment=(string) au, "
      "stream-format=(string) byte-stream, "
      "width=(int) [1,MAX], " "height=(int) [1,MAX]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.265 Video Decoder",
      "Codec/Decoder/Video",
      "Decode H.265 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videodec_class->cdata, "video_decoder.hevc");
}

static void
gst_omx_h265_dec_init (GstOMXH265Dec * self)
{
}

static gboolean
gst_omx_h265_dec_is_format_change (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state)
{
  GstCaps *old_caps = NULL;
  GstCaps *new_caps = state->caps;
  GstStructure *old_structure, *new_structure;
  const gchar *old_profile, *old_level, *old_tier, *new_profile, *new_level,
      *new_tier;

  if (dec->input_state) {
    old_caps = dec->input_state->caps;
  }

  if (!old_caps) {
    return FALSE;
  }

  old_structure = gst_caps_get_structure (old_caps, 0);
  new_structure = gst_caps_get_structure (new_caps, 0);
  old_profile = gst_structure_get_string (old_structure, "profile");
  old_level = gst_structure_get_string (old_structure, "level");
  old_tier = gst_structure_get_string (old_structure, "tier");
  new_profile = gst_structure_get_string (new_structure, "profile");
  new_level = gst_structure_get_string (new_structure, "level");
  new_tier = gst_structure_get_string (new_structure, "tier");

  if (g_strcmp0 (old_profile, new_profile) != 0
      || g_strcmp0 (old_level, new_level) != 0
      || g_strcmp0 (old_tier, new_tier)) {
    return TRUE;
  }

  return FALSE;
}

static gboolean
set_profile_and_level (GstOMXH265Dec * self, GstVideoCodecState * state)
{
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile_string, *level_string, *tier_string;
  GstStructure *s;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_DEC (self)->dec_in_port->index;

  /* Pass profile, level and tier to the decoder if we have all info from the
   * caps. */
  s = gst_caps_get_structure (state->caps, 0);
  profile_string = gst_structure_get_string (s, "profile");
  if (!profile_string)
    return TRUE;

  param.eProfile = gst_omx_h265_utils_get_profile_from_str (profile_string);
  if (param.eProfile == OMX_VIDEO_HEVCProfileUnknown)
    goto unsupported_profile;

  level_string = gst_structure_get_string (s, "level");
  tier_string = gst_structure_get_string (s, "tier");
  if (!level_string || !tier_string)
    return TRUE;

  param.eLevel =
      gst_omx_h265_utils_get_level_from_str (level_string, tier_string);
  if (param.eLevel == OMX_VIDEO_HEVCLevelUnknown)
    goto unsupported_level;

  GST_DEBUG_OBJECT (self,
      "Set profile (%s) level (%s) and tier (%s) on decoder", profile_string,
      level_string, tier_string);

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_DEC (self)->dec,
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
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
  return FALSE;
}

static gboolean
gst_omx_h265_dec_set_format (GstOMXVideoDec * dec, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_ERRORTYPE err;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.video.eCompressionFormat =
      (OMX_VIDEO_CODINGTYPE) OMX_VIDEO_CodingHEVC;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone)
    return FALSE;

  if (klass->cdata.hacks & GST_OMX_HACK_PASS_PROFILE_TO_DECODER) {
    if (!set_profile_and_level (GST_OMX_H265_DEC (dec), state))
      return FALSE;
  }

  return TRUE;
}
