/*
 * Copyright (C) 2014 SUMOMO Computer Association
 *     Author: ayaka <ayaka@soulik.info>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "gstv4l2object.h"
#include "gstv4l2h264enc.h"

#include <string.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_v4l2_h264_enc_debug);
#define GST_CAT_DEFAULT gst_v4l2_h264_enc_debug


static GstStaticCaps src_template_caps =
GST_STATIC_CAPS ("video/x-h264, stream-format=(string) byte-stream, "
    "alignment=(string) au");

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
/* TODO add H264 controls
 * PROP_I_FRAME_QP,
 * PROP_P_FRAME_QP,
 * PROP_B_FRAME_QP,
 * PROP_MIN_QP,
 * PROP_MAX_QP,
 * PROP_8x8_TRANSFORM,
 * PROP_CPB_SIZE,
 * PROP_ENTROPY_MODE,
 * PROP_I_PERIOD,
 * PROP_LOOP_FILTER_ALPHA,
 * PROP_LOOP_FILTER_BETA,
 * PROP_LOOP_FILTER_MODE,
 * PROP_VUI_EXT_SAR_HEIGHT,
 * PROP_VUI_EXT_SAR_WIDTH,
 * PROP_VUI_SAR_ENABLED,
 * PROP_VUI_SAR_IDC,
 * PROP_SEI_FRAME_PACKING,
 * PROP_SEI_FP_CURRENT_FRAME_0,
 * PROP_SEI_FP_ARRANGEMENT_TYP,
 * ...
 * */
};

#define gst_v4l2_h264_enc_parent_class parent_class
G_DEFINE_TYPE (GstV4l2H264Enc, gst_v4l2_h264_enc, GST_TYPE_V4L2_VIDEO_ENC);

static void
gst_v4l2_h264_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  /* TODO */
}

static void
gst_v4l2_h264_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  /* TODO */
}

static gint
v4l2_profile_from_string (const gchar * profile)
{
  gint v4l2_profile = -1;

  if (g_str_equal (profile, "baseline")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
  } else if (g_str_equal (profile, "constrained-baseline")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE;
  } else if (g_str_equal (profile, "main")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
  } else if (g_str_equal (profile, "extended")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED;
  } else if (g_str_equal (profile, "high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
  } else if (g_str_equal (profile, "high-10")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;
  } else if (g_str_equal (profile, "high-4:2:2")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422;
  } else if (g_str_equal (profile, "high-4:4:4")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE;
  } else if (g_str_equal (profile, "high-10-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10_INTRA;
  } else if (g_str_equal (profile, "high-4:2:2-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA;
  } else if (g_str_equal (profile, "high-4:4:4-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_INTRA;
  } else if (g_str_equal (profile, "cavlc-4:4:4-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_CAVLC_444_INTRA;
  } else if (g_str_equal (profile, "scalable-baseline")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_BASELINE;
  } else if (g_str_equal (profile, "scalable-high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH;
  } else if (g_str_equal (profile, "scalable-high-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH_INTRA;
  } else if (g_str_equal (profile, "stereo-high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH;
  } else if (g_str_equal (profile, "multiview-high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH;
  } else {
    GST_WARNING ("Unsupported profile string '%s'", profile);
  }

  return v4l2_profile;
}

static const gchar *
v4l2_profile_to_string (gint v4l2_profile)
{
  switch (v4l2_profile) {
    case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
      return "baseline";
    case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
      return "constrained-baseline";
    case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
      return "main";
    case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
      return "extended";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
      return "high";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
      return "high-10";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422:
      return "high-4:2:2";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE:
      return "high-4:4:4";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10_INTRA:
      return "high-10-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA:
      return "high-4:2:2-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_INTRA:
      return "high-4:4:4-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_CAVLC_444_INTRA:
      return "cavlc-4:4:4-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_BASELINE:
      return "scalable-baseline";
    case V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH:
      return "scalable-high";
    case V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH_INTRA:
      return "scalable-high-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH:
      return "stereo-high";
    case V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH:
      return "multiview-high";
    default:
      GST_WARNING ("Unsupported V4L2 profile %i", v4l2_profile);
      break;
  }

  return NULL;
}

static gint
v4l2_level_from_string (const gchar * level)
{
  gint v4l2_level = -1;

  if (g_str_equal (level, "1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
  else if (g_str_equal (level, "1b"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1B;
  else if (g_str_equal (level, "1.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
  else if (g_str_equal (level, "1.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
  else if (g_str_equal (level, "1.3"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
  else if (g_str_equal (level, "2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
  else if (g_str_equal (level, "2.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
  else if (g_str_equal (level, "2.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
  else if (g_str_equal (level, "3"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
  else if (g_str_equal (level, "3.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
  else if (g_str_equal (level, "3.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
  else if (g_str_equal (level, "4"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
  else if (g_str_equal (level, "4.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
  else if (g_str_equal (level, "4.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
  else if (g_str_equal (level, "5"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
  else if (g_str_equal (level, "5.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
  else
    GST_WARNING ("Unsupported level '%s'", level);

  return v4l2_level;
}

static const gchar *
v4l2_level_to_string (gint v4l2_level)
{
  switch (v4l2_level) {
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
      return "1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
      return "1b";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
      return "1.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
      return "1.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
      return "1.3";
    case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
      return "2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
      return "2.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
      return "2.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
      return "3.0";
    case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
      return "3.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
      return "3.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
      return "4";
    case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
      return "4.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
      return "4.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
      return "5";
    case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
      return "5.1";
    default:
      GST_WARNING ("Unsupported V4L2 level %i", v4l2_level);
      break;
  }

  return NULL;
}

static void
gst_v4l2_h264_enc_init (GstV4l2H264Enc * self)
{
}

static void
gst_v4l2_h264_enc_class_init (GstV4l2H264EncClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstV4l2VideoEncClass *baseclass;

  parent_class = g_type_class_peek_parent (klass);

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  baseclass = (GstV4l2VideoEncClass *) (klass);

  GST_DEBUG_CATEGORY_INIT (gst_v4l2_h264_enc_debug, "v4l2h264enc", 0,
      "V4L2 H.264 Encoder");

  gst_element_class_set_static_metadata (element_class,
      "V4L2 H.264 Encoder",
      "Codec/Encoder/Video",
      "Encode H.264 video streams via V4L2 API", "ayaka <ayaka@soulik.info>");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_h264_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_h264_enc_get_property);

  baseclass->codec_name = "H264";
  baseclass->profile_cid = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
  baseclass->profile_to_string = v4l2_profile_to_string;
  baseclass->profile_from_string = v4l2_profile_from_string;
  baseclass->level_cid = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
  baseclass->level_to_string = v4l2_level_to_string;
  baseclass->level_from_string = v4l2_level_from_string;
}

/* Probing functions */
gboolean
gst_v4l2_is_h264_enc (GstCaps * sink_caps, GstCaps * src_caps)
{
  return gst_v4l2_is_video_enc (sink_caps, src_caps,
      gst_static_caps_get (&src_template_caps));
}

void
gst_v4l2_h264_enc_register (GstPlugin * plugin, const gchar * basename,
    const gchar * device_path, GstCaps * sink_caps, GstCaps * src_caps)
{
  gst_v4l2_video_enc_register (plugin, GST_TYPE_V4L2_H264_ENC,
      "h264", basename, device_path, sink_caps,
      gst_static_caps_get (&src_template_caps), src_caps);
}
