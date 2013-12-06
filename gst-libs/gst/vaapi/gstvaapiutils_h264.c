/*
 *  gstvaapiutils_h264.c - H.264 related utilities
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapiutils_h264.h"

/* Table A-1 - Level limits */
/* *INDENT-OFF* */
static const GstVaapiH264LevelLimits gst_vaapi_h264_level_limits[] = {
  /* level                     idc   MaxMBPS   MaxFS MaxDpbMbs   MaxBR */
  { GST_VAAPI_LEVEL_H264_L1,    10,     1485,     99,     396,      64 },
  { GST_VAAPI_LEVEL_H264_L1b,   11,     1485,     99,     396,     128 },
  { GST_VAAPI_LEVEL_H264_L1_1,  11,     3000,    396,     900,     192 },
  { GST_VAAPI_LEVEL_H264_L1_2,  12,     6000,    396,    2376,     384 },
  { GST_VAAPI_LEVEL_H264_L1_3,  13,    11880,    396,    2376,     768 },
  { GST_VAAPI_LEVEL_H264_L2,    20,    11880,    396,    2376,    2000 },
  { GST_VAAPI_LEVEL_H264_L2_1,  21,    19800,    792,    4752,    4000 },
  { GST_VAAPI_LEVEL_H264_L2_2,  22,    20250,   1620,    8100,    4000 },
  { GST_VAAPI_LEVEL_H264_L3,    30,    40500,   1620,    8100,   10000 },
  { GST_VAAPI_LEVEL_H264_L3_1,  31,   108000,   3600,   18000,   14000 },
  { GST_VAAPI_LEVEL_H264_L3_2,  32,   216000,   5120,   20480,   20000 },
  { GST_VAAPI_LEVEL_H264_L4,    40,   245760,   8192,   32768,   20000 },
  { GST_VAAPI_LEVEL_H264_L4_1,  41,   245760,   8192,   32768,   50000 },
  { GST_VAAPI_LEVEL_H264_L4_2,  42,   522240,   8704,   34816,   50000 },
  { GST_VAAPI_LEVEL_H264_L5,    50,   589824,  22080,  110400,  135000 },
  { GST_VAAPI_LEVEL_H264_L5_1,  51,   983040,  36864,  184320,  240000 },
  { GST_VAAPI_LEVEL_H264_L5_2,  52,  2073600,  36864,  184320,  240000 },
  { 0, }
};
/* *INDENT-ON* */

/** Returns GstVaapiProfile from H.264 profile_idc value */
GstVaapiProfile
gst_vaapi_utils_h264_get_profile (guint8 profile_idc)
{
  GstVaapiProfile profile;

  switch (profile_idc) {
    case GST_H264_PROFILE_BASELINE:
      profile = GST_VAAPI_PROFILE_H264_BASELINE;
      break;
    case GST_H264_PROFILE_MAIN:
      profile = GST_VAAPI_PROFILE_H264_MAIN;
      break;
    case GST_H264_PROFILE_HIGH:
      profile = GST_VAAPI_PROFILE_H264_HIGH;
      break;
    case GST_H264_PROFILE_HIGH10:
      profile = GST_VAAPI_PROFILE_H264_HIGH10;
      break;
    default:
      g_assert (0 && "unsupported profile_idc value");
      profile = GST_VAAPI_PROFILE_UNKNOWN;
      break;
  }
  return profile;
}

/** Returns H.264 profile_idc value from GstVaapiProfile */
guint8
gst_vaapi_utils_h264_get_profile_idc (GstVaapiProfile profile)
{
  guint8 profile_idc;

  switch (profile) {
    case GST_VAAPI_PROFILE_H264_BASELINE:
    case GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE:
      profile_idc = GST_H264_PROFILE_BASELINE;
      break;
    case GST_VAAPI_PROFILE_H264_MAIN:
      profile_idc = GST_H264_PROFILE_MAIN;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH:
      profile_idc = GST_H264_PROFILE_HIGH;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH10:
      profile_idc = GST_H264_PROFILE_HIGH10;
      break;
    default:
      g_assert (0 && "unsupported GstVaapiProfile value");
      profile_idc = 0;
      break;
  }
  return profile_idc;
}

/** Returns GstVaapiLevelH264 from H.264 level_idc value */
GstVaapiLevelH264
gst_vaapi_utils_h264_get_level (guint8 level_idc)
{
  const GstVaapiH264LevelLimits *llp;

  // Prefer Level 1.1 over level 1b
  if (G_UNLIKELY (level_idc == 11))
    return GST_VAAPI_LEVEL_H264_L1_1;

  for (llp = gst_vaapi_h264_level_limits; llp->level != 0; llp++) {
    if (llp->level_idc == level_idc)
      return llp->level;
  }
  g_assert (0 && "unsupported level_idc value");
  return (GstVaapiLevelH264) 0;
}

/** Returns H.264 level_idc value from GstVaapiLevelH264 */
guint8
gst_vaapi_utils_h264_get_level_idc (GstVaapiLevelH264 level)
{
  const GstVaapiH264LevelLimits *const llp =
     gst_vaapi_utils_h264_get_level_limits (level);

  return llp ? llp->level_idc : 0;
}

/** Returns level limits as specified in Table A-1 of the H.264 standard */
const GstVaapiH264LevelLimits *
gst_vaapi_utils_h264_get_level_limits (GstVaapiLevelH264 level)
{
  if (level < GST_VAAPI_LEVEL_H264_L1 || level > GST_VAAPI_LEVEL_H264_L5_2)
    return NULL;
  return &gst_vaapi_h264_level_limits[level - GST_VAAPI_LEVEL_H264_L1];
}

/** Returns the Table A-1 specification */
const GstVaapiH264LevelLimits *
gst_vaapi_utils_h264_get_level_limits_table (guint * out_length_ptr)
{
  if (out_length_ptr)
    *out_length_ptr = G_N_ELEMENTS (gst_vaapi_h264_level_limits) - 1;
  return gst_vaapi_h264_level_limits;
}

/** Returns GstVaapiChromaType from H.264 chroma_format_idc value */
GstVaapiChromaType
gst_vaapi_utils_h264_get_chroma_type (guint chroma_format_idc)
{
  GstVaapiChromaType chroma_type;

  switch (chroma_format_idc) {
    case 0:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV400;
      break;
    case 1:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
      break;
    case 2:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
      break;
    case 3:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
      break;
    default:
      g_assert (0 && "unsupported chroma_format_idc value");
      chroma_type = (GstVaapiChromaType) 0;
      break;
  }
  return chroma_type;
}

/** Returns H.264 chroma_format_idc value from GstVaapiChromaType */
guint
gst_vaapi_utils_h264_get_chroma_format_idc (GstVaapiChromaType chroma_type)
{
  guint chroma_format_idc;

  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV400:
      chroma_format_idc = 0;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV420:
      chroma_format_idc = 1;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
      chroma_format_idc = 2;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
      chroma_format_idc = 3;
      break;
    default:
      g_assert (0 && "unsupported GstVaapiChromaType value");
      chroma_format_idc = 1;
      break;
  }
  return chroma_format_idc;
}
