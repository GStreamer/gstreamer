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

#include "gstomxh264utils.h"

OMX_VIDEO_AVCPROFILETYPE
gst_omx_h264_utils_get_profile_from_str (const gchar * profile)
{
  if (g_str_equal (profile, "baseline")) {
    return OMX_VIDEO_AVCProfileBaseline;
  } else if (g_str_equal (profile, "main")) {
    return OMX_VIDEO_AVCProfileMain;
  } else if (g_str_equal (profile, "extended")) {
    return OMX_VIDEO_AVCProfileExtended;
  } else if (g_str_equal (profile, "high")) {
    return OMX_VIDEO_AVCProfileHigh;
  } else if (g_str_equal (profile, "high-10")) {
    return OMX_VIDEO_AVCProfileHigh10;
  } else if (g_str_equal (profile, "high-4:2:2")) {
    return OMX_VIDEO_AVCProfileHigh422;
  } else if (g_str_equal (profile, "high-4:4:4")) {
    return OMX_VIDEO_AVCProfileHigh444;
  }

  return OMX_VIDEO_AVCProfileMax;
}

OMX_VIDEO_AVCLEVELTYPE
gst_omx_h264_utils_get_level_from_str (const gchar * level)
{
  if (g_str_equal (level, "1")) {
    return OMX_VIDEO_AVCLevel1;
  } else if (g_str_equal (level, "1b")) {
    return OMX_VIDEO_AVCLevel1b;
  } else if (g_str_equal (level, "1.1")) {
    return OMX_VIDEO_AVCLevel11;
  } else if (g_str_equal (level, "1.2")) {
    return OMX_VIDEO_AVCLevel12;
  } else if (g_str_equal (level, "1.3")) {
    return OMX_VIDEO_AVCLevel13;
  } else if (g_str_equal (level, "2")) {
    return OMX_VIDEO_AVCLevel2;
  } else if (g_str_equal (level, "2.1")) {
    return OMX_VIDEO_AVCLevel21;
  } else if (g_str_equal (level, "2.2")) {
    return OMX_VIDEO_AVCLevel22;
  } else if (g_str_equal (level, "3")) {
    return OMX_VIDEO_AVCLevel3;
  } else if (g_str_equal (level, "3.1")) {
    return OMX_VIDEO_AVCLevel31;
  } else if (g_str_equal (level, "3.2")) {
    return OMX_VIDEO_AVCLevel32;
  } else if (g_str_equal (level, "4")) {
    return OMX_VIDEO_AVCLevel4;
  } else if (g_str_equal (level, "4.1")) {
    return OMX_VIDEO_AVCLevel41;
  } else if (g_str_equal (level, "4.2")) {
    return OMX_VIDEO_AVCLevel42;
  } else if (g_str_equal (level, "5")) {
    return OMX_VIDEO_AVCLevel5;
  } else if (g_str_equal (level, "5.1")) {
    return OMX_VIDEO_AVCLevel51;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  } else if (g_str_equal (level, "5.2")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel52;
  } else if (g_str_equal (level, "6.0")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel60;
  } else if (g_str_equal (level, "6.1")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel61;
  } else if (g_str_equal (level, "6.2")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel62;
#endif
  }

  return OMX_VIDEO_AVCLevelMax;
}
