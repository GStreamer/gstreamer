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

#include "gstomxh264dec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_h264_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_h264_dec_debug_category

/* prototypes */
static gboolean gst_omx_h264_dec_is_format_change (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state);
static gboolean gst_omx_h264_dec_set_format (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h264_dec_debug_category, "omxh264dec", 0, \
      "debug category for gst-omx video decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXH264Dec, gst_omx_h264_dec,
    GST_TYPE_OMX_VIDEO_DEC, DEBUG_INIT);

static void
gst_omx_h264_dec_class_init (GstOMXH264DecClass * klass)
{
  GstOMXVideoDecClass *videodec_class = GST_OMX_VIDEO_DEC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  videodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_h264_dec_is_format_change);
  videodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h264_dec_set_format);

  videodec_class->cdata.default_sink_template_caps = "video/x-h264, "
      "parsed=(boolean) true, "
      "alignment=(string) au, "
      "stream-format=(string) byte-stream, "
      "width=(int) [1,MAX], " "height=(int) [1,MAX]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.264 Video Decoder",
      "Codec/Decoder/Video",
      "Decode H.264 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videodec_class->cdata, "video_decoder.avc");
}

static void
gst_omx_h264_dec_init (GstOMXH264Dec * self)
{
}

static gboolean
gst_omx_h264_dec_is_format_change (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state)
{
  GstCaps *old_caps = NULL;
  GstCaps *new_caps = state->caps;
  GstStructure *old_structure, *new_structure;
  const gchar *old_profile, *old_level, *new_profile, *new_level;

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
  new_profile = gst_structure_get_string (new_structure, "profile");
  new_level = gst_structure_get_string (new_structure, "level");

  if (g_strcmp0 (old_profile, new_profile) != 0
      || g_strcmp0 (old_level, new_level) != 0) {
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_omx_h264_dec_set_format (GstOMXVideoDec * dec, GstOMXPort * port,
    GstVideoCodecState * state)
{
  gboolean ret;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
  ret = gst_omx_port_update_port_definition (port, &port_def) == OMX_ErrorNone;

  return ret;
}
