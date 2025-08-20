/*
 * Copyright (C) 2022 Synaptics Incorporated
 *    Author: Hsia-Jun(Randy) Li <randy.li@synaptics.com>
 * Copyright (C) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 *    Author: Deepa Guthyappa Madivalara <deepa.madivalara@oss.qualcomm.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstv4l2av1codec.h"

#include <gst/gst.h>
#include "ext/v4l2-controls.h"

static gint
v4l2_profile_from_string (const gchar * profile)
{
  gint v4l2_profile = -1;

  if (g_str_equal (profile, "main"))
    v4l2_profile = V4L2_MPEG_VIDEO_AV1_PROFILE_MAIN;
  else if (g_str_equal (profile, "high"))
    v4l2_profile = V4L2_MPEG_VIDEO_AV1_PROFILE_HIGH;
  else if (g_str_equal (profile, "professional"))
    v4l2_profile = V4L2_MPEG_VIDEO_AV1_PROFILE_PROFESSIONAL;
  else
    GST_WARNING ("Unsupported profile string '%s'", profile);

  return v4l2_profile;
}

static const gchar *
v4l2_profile_to_string (gint v4l2_profile)
{
  switch (v4l2_profile) {
    case V4L2_MPEG_VIDEO_AV1_PROFILE_MAIN:
      return "main";
    case V4L2_MPEG_VIDEO_AV1_PROFILE_HIGH:
      return "high";
    case V4L2_MPEG_VIDEO_AV1_PROFILE_PROFESSIONAL:
      return "professional";
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

  if (g_str_equal (level, "2.0"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_2_0;
  else if (g_str_equal (level, "2.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_2_1;
  else if (g_str_equal (level, "2.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_2_2;
  else if (g_str_equal (level, "2.3"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_2_3;
  else if (g_str_equal (level, "3.0"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_3_0;
  else if (g_str_equal (level, "3.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_3_1;
  else if (g_str_equal (level, "3.2"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_3_2;
  else if (g_str_equal (level, "3.2"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_3_3;
  else if (g_str_equal (level, "4.0"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_4_0;
  else if (g_str_equal (level, "4.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_4_1;
  else if (g_str_equal (level, "4.2"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_4_2;
  else if (g_str_equal (level, "4.3"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_4_3;
  else if (g_str_equal (level, "5.0"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_5_0;
  else if (g_str_equal (level, "5.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_5_1;
  else if (g_str_equal (level, "5.2"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_5_2;
  else if (g_str_equal (level, "5.3"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_5_3;
  else if (g_str_equal (level, "6.0"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_6_0;
  else if (g_str_equal (level, "6.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_6_1;
  else if (g_str_equal (level, "6.2"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_6_2;
  else if (g_str_equal (level, "6.3"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_6_3;
  else if (g_str_equal (level, "7.0"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_7_0;
  else if (g_str_equal (level, "7.1"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_7_1;
  else if (g_str_equal (level, "7.2"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_7_2;
  else if (g_str_equal (level, "7.3"))
    v4l2_level = V4L2_MPEG_VIDEO_AV1_LEVEL_7_3;
  else
    GST_WARNING ("Unsupported level '%s'", level);

  return v4l2_level;
}

static const gchar *
v4l2_level_to_string (gint v4l2_level)
{
  switch (v4l2_level) {
    case V4L2_MPEG_VIDEO_AV1_LEVEL_2_0:
      return "2.0";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_2_1:
      return "2.1";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_2_2:
      return "2.2";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_2_3:
      return "2.3";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_3_0:
      return "3.0";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_3_1:
      return "3.1";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_3_2:
      return "3.2";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_3_3:
      return "3.3";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_4_0:
      return "4.0";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_4_1:
      return "4.1";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_4_2:
      return "4.2";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_4_3:
      return "4.3";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_5_0:
      return "5.0";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_5_1:
      return "5.1";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_5_2:
      return "5.2";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_5_3:
      return "5.3";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_6_0:
      return "6.0";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_6_1:
      return "6.1";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_6_2:
      return "6.2";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_6_3:
      return "6.3";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_7_0:
      return "7.0";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_7_1:
      return "7.1";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_7_2:
      return "7.2";
    case V4L2_MPEG_VIDEO_AV1_LEVEL_7_3:
      return "7.3";
    default:
      GST_WARNING ("Unsupported V4L2 level %i", v4l2_level);
      break;
  }

  return NULL;
}

const GstV4l2Codec *
gst_v4l2_av1_get_codec (void)
{
  static GstV4l2Codec *codec = NULL;
  if (g_once_init_enter (&codec)) {
    static GstV4l2Codec c;
    c.profile_cid = V4L2_CID_MPEG_VIDEO_AV1_PROFILE;
    c.profile_to_string = v4l2_profile_to_string;
    c.profile_from_string = v4l2_profile_from_string;
    c.level_cid = V4L2_CID_MPEG_VIDEO_AV1_LEVEL;
    c.level_to_string = v4l2_level_to_string;
    c.level_from_string = v4l2_level_from_string;
    g_once_init_leave (&codec, &c);
  }
  return codec;
}
