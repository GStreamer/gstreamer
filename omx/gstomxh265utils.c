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

#include "gstomxh265utils.h"

OMX_VIDEO_HEVCPROFILETYPE
gst_omx_h265_utils_get_profile_from_str (const gchar * profile)
{
  if (g_str_equal (profile, "main")) {
    return OMX_VIDEO_HEVCProfileMain;
  } else if (g_str_equal (profile, "main-10")) {
    return OMX_VIDEO_HEVCProfileMain10;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  } else if (g_str_equal (profile, "main-still-picture")) {
    return (OMX_VIDEO_HEVCPROFILETYPE) OMX_ALG_VIDEO_HEVCProfileMainStill;
  } else if (g_str_equal (profile, "main-422")) {
    /* Not standard: 8 bits variation of main-422-10 */
    return (OMX_VIDEO_HEVCPROFILETYPE) OMX_ALG_VIDEO_HEVCProfileMain422;
  } else if (g_str_equal (profile, "main-422-10")) {
    return (OMX_VIDEO_HEVCPROFILETYPE) OMX_ALG_VIDEO_HEVCProfileMain422_10;
#endif
  }
  return OMX_VIDEO_HEVCProfileUnknown;
}

OMX_VIDEO_HEVCLEVELTYPE
gst_omx_h265_utils_get_level_from_str (const gchar * level, const gchar * tier)
{
  if (g_str_equal (tier, "main")) {
    if (g_str_equal (level, "1"))
      return OMX_VIDEO_HEVCMainTierLevel1;
    else if (g_str_equal (level, "2"))
      return OMX_VIDEO_HEVCMainTierLevel2;
    else if (g_str_equal (level, "2.1"))
      return OMX_VIDEO_HEVCMainTierLevel21;
    else if (g_str_equal (level, "3"))
      return OMX_VIDEO_HEVCMainTierLevel3;
    else if (g_str_equal (level, "3.1"))
      return OMX_VIDEO_HEVCMainTierLevel31;
    else if (g_str_equal (level, "4"))
      return OMX_VIDEO_HEVCMainTierLevel4;
    else if (g_str_equal (level, "4.1"))
      return OMX_VIDEO_HEVCMainTierLevel41;
    else if (g_str_equal (level, "5"))
      return OMX_VIDEO_HEVCMainTierLevel5;
    else if (g_str_equal (level, "5.1"))
      return OMX_VIDEO_HEVCMainTierLevel51;
    else if (g_str_equal (level, "5.2"))
      return OMX_VIDEO_HEVCMainTierLevel52;
    else if (g_str_equal (level, "6"))
      return OMX_VIDEO_HEVCMainTierLevel6;
    else if (g_str_equal (level, "6.1"))
      return OMX_VIDEO_HEVCMainTierLevel61;
    else if (g_str_equal (level, "6.2"))
      return OMX_VIDEO_HEVCMainTierLevel62;
  } else if (g_str_equal (tier, "high")) {
    if (g_str_equal (level, "4"))
      return OMX_VIDEO_HEVCHighTierLevel4;
    else if (g_str_equal (level, "4.1"))
      return OMX_VIDEO_HEVCHighTierLevel41;
    else if (g_str_equal (level, "5"))
      return OMX_VIDEO_HEVCHighTierLevel5;
    else if (g_str_equal (level, "5.1"))
      return OMX_VIDEO_HEVCHighTierLevel51;
    else if (g_str_equal (level, "5.2"))
      return OMX_VIDEO_HEVCHighTierLevel52;
    else if (g_str_equal (level, "6"))
      return OMX_VIDEO_HEVCHighTierLevel6;
    else if (g_str_equal (level, "6.1"))
      return OMX_VIDEO_HEVCHighTierLevel61;
    else if (g_str_equal (level, "6.2"))
      return OMX_VIDEO_HEVCHighTierLevel62;
  }

  return OMX_VIDEO_HEVCLevelUnknown;
}
