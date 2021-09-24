/*
 *  gstvaapiutils_h265.c - H.265 related utilities
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
#include <gst/codecparsers/gsth265parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiutils_h265_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

struct map
{
  guint value;
  const gchar *name;
};

/* Profile string map */
static const struct map gst_vaapi_h265_profile_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_PROFILE_H265_MAIN,                 "main"                 },
  { GST_VAAPI_PROFILE_H265_MAIN10,               "main-10"              },
  { GST_VAAPI_PROFILE_H265_MAIN_STILL_PICTURE,   "main-still-picture"   },
  { GST_VAAPI_PROFILE_H265_MAIN_444,             "main-444"             },
  { GST_VAAPI_PROFILE_H265_MAIN_444_10,          "main-444-10"          },
  { GST_VAAPI_PROFILE_H265_MAIN_422_10,          "main-422-10"          },
  { GST_VAAPI_PROFILE_H265_MAIN12,               "main-12"              },
  { GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN,        "screen-extended-main"       },
  { GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_10,     "screen-extended-main-10"    },
  { GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444,    "screen-extended-main-444"   },
  { GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444_10, "screen-extended-main-444-10"},
  { 0, NULL }
/* *INDENT-ON* */
};

/* Tier string map */
static const struct map gst_vaapi_h265_tier_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_TIER_H265_MAIN,    "main" },
  { GST_VAAPI_TIER_H265_HIGH,    "high"},
  { GST_VAAPI_TIER_H265_UNKNOWN, "unknown"}
/* *INDENT-ON* */
};

/* Level string map */
static const struct map gst_vaapi_h265_level_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_LEVEL_H265_L1,    "1"     },
  { GST_VAAPI_LEVEL_H265_L2,    "2"     },
  { GST_VAAPI_LEVEL_H265_L2_1,  "2.1"   },
  { GST_VAAPI_LEVEL_H265_L3,    "3"     },
  { GST_VAAPI_LEVEL_H265_L3_1,  "3.1"   },
  { GST_VAAPI_LEVEL_H265_L4,    "4"     },
  { GST_VAAPI_LEVEL_H265_L4_1,  "4.1"   },
  { GST_VAAPI_LEVEL_H265_L5,    "5"     },
  { GST_VAAPI_LEVEL_H265_L5_1,  "5.1"   },
  { GST_VAAPI_LEVEL_H265_L5_2,  "5.2"   },
  { GST_VAAPI_LEVEL_H265_L6,    "6"     },
  { GST_VAAPI_LEVEL_H265_L6_1,  "6.1"   },
  { GST_VAAPI_LEVEL_H265_L6_2,  "6.2"   },
  { 0, NULL }
/* *INDENT-ON* */
};

/* Table A-1 - Level limits */
/* *INDENT-OFF* */
static const GstVaapiH265LevelLimits gst_vaapi_h265_level_limits[] = {
  /* level                     idc   MaxLumaPs  MCPBMt  MCPBHt MSlSeg MTR MTC   MaxLumaSr   MBRMt   MBRHt MinCr*/
  { GST_VAAPI_LEVEL_H265_L1,    30,     36864,    350,      0,    16,  1,  1,     552960,    128,      0,  2},
  { GST_VAAPI_LEVEL_H265_L2,    60,    122880,   1500,      0,    16,  1,  1,    3686400,   1500,      0,  2},
  { GST_VAAPI_LEVEL_H265_L2_1,  63,    245760,   3000,      0,    20,  1,  1,    7372800,   3000,      0,  2},
  { GST_VAAPI_LEVEL_H265_L3,    90,    552960,   6000,      0,    30,  2,  2,   16588800,   6000,      0,  2},
  { GST_VAAPI_LEVEL_H265_L3_1,  93,    983040,  10000,      0,    40,  3,  3,   33177600,  10000,      0,  2},
  { GST_VAAPI_LEVEL_H265_L4,    120,  2228224,  12000,  30000,    75,  5,  5,   66846720,  12000,  30000,  4},
  { GST_VAAPI_LEVEL_H265_L4_1,  123,  2228224,  20000,  50000,    75,  5,  5,  133693440,  20000,  50000,  4},
  { GST_VAAPI_LEVEL_H265_L5,    150,  8912896,  25000, 100000,   200, 11, 10,  267386880,  25000, 100000,  6},
  { GST_VAAPI_LEVEL_H265_L5_1,  153,  8912896,  40000, 160000,   200, 11, 10,  534773760,  40000, 160000,  8},
  { GST_VAAPI_LEVEL_H265_L5_2,  156,  8912896,  60000, 240000,   200, 11, 10, 1069547520,  60000, 240000,  8},
  { GST_VAAPI_LEVEL_H265_L6,    180, 35651584,  60000, 240000,   600, 22, 20, 1069547520,  60000, 240000,  8},
  { GST_VAAPI_LEVEL_H265_L6_1,  183, 35651584, 120000, 480000,   600, 22, 20, 2139095040, 120000, 480000,  8},
  { GST_VAAPI_LEVEL_H265_L6_2,  186, 35651584, 240000, 800000,   600, 22, 20, 4278190080, 240000, 800000,  6},
  { 0, }
};
/* *INDENT-ON* */

/* Lookup value in map */
static const struct map *
map_lookup_value (const struct map *m, guint value)
{
  g_return_val_if_fail (m != NULL, NULL);

  for (; m->name != NULL; m++) {
    if (m->value == value)
      return m;
  }
  return NULL;
}

/* Lookup name in map */
static const struct map *
map_lookup_name (const struct map *m, const gchar * name)
{
  g_return_val_if_fail (m != NULL, NULL);

  if (!name)
    return NULL;

  for (; m->name != NULL; m++) {
    if (strcmp (m->name, name) == 0)
      return m;
  }
  return NULL;
}

/** Returns a relative score for the supplied GstVaapiProfile */
guint
gst_vaapi_utils_h265_get_profile_score (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_h265_profile_map, profile);

  return m ? 1 + (m - gst_vaapi_h265_profile_map) : 0;
}

/** Returns GstVaapiProfile from H.265 profile_idc value */
GstVaapiProfile
gst_vaapi_utils_h265_get_profile (GstH265SPS * sps)
{
  GstVaapiProfile vaapi_profile;
  GstH265Profile profile;

  g_return_val_if_fail (sps != NULL, GST_VAAPI_PROFILE_UNKNOWN);

  profile = gst_h265_get_profile_from_sps (sps);
  switch (profile) {
    case GST_H265_PROFILE_MAIN:
      /* Main Intra, recognize it as MAIN */
    case GST_H265_PROFILE_MAIN_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN;
      break;
    case GST_H265_PROFILE_MAIN_10:
      /* Main 10 Intra, recognize it as MAIN10 */
    case GST_H265_PROFILE_MAIN_10_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN10;
      break;
    case GST_H265_PROFILE_MAIN_12:
      /* Main 12 Intra, recognize it as MAIN_12 */
    case GST_H265_PROFILE_MAIN_12_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN12;
      break;
    case GST_H265_PROFILE_MAIN_STILL_PICTURE:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN_STILL_PICTURE;
      break;
    case GST_H265_PROFILE_MAIN_422_10:
      /* Main 422_10 Intra, recognize it as MAIN_422_10 */
    case GST_H265_PROFILE_MAIN_422_10_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN_422_10;
      break;
    case GST_H265_PROFILE_MAIN_422_12:
      /* Main 422_12 Intra, recognize it as MAIN_422_12 */
    case GST_H265_PROFILE_MAIN_422_12_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN_422_12;
      break;
    case GST_H265_PROFILE_MAIN_444:
      /* Main 444 Intra, recognize it as MAIN_444 */
    case GST_H265_PROFILE_MAIN_444_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN_444;
      break;
    case GST_H265_PROFILE_MAIN_444_10:
      /* Main 444_10 Intra, recognize it as MAIN_444_10 */
    case GST_H265_PROFILE_MAIN_444_10_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN_444_10;
      break;
    case GST_H265_PROFILE_MAIN_444_12:
      /* Main 444_12 Intra, recognize it as MAIN_444_12 */
    case GST_H265_PROFILE_MAIN_444_12_INTRA:
      vaapi_profile = GST_VAAPI_PROFILE_H265_MAIN_444_12;
      break;
    case GST_H265_PROFILE_SCREEN_EXTENDED_MAIN:
      vaapi_profile = GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN;
      break;
    case GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_10:
      vaapi_profile = GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_10;
      break;
    case GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_444:
      vaapi_profile = GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444;
      break;
    case GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_444_10:
      vaapi_profile = GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444_10;
      break;
    default:
      GST_DEBUG ("unsupported profile_idc value");
      vaapi_profile = GST_VAAPI_PROFILE_UNKNOWN;
      break;
  }
  return vaapi_profile;
}

/** Returns H.265 profile_idc value from GstVaapiProfile */
guint8
gst_vaapi_utils_h265_get_profile_idc (GstVaapiProfile profile)
{
  guint8 profile_idc;

  switch (profile) {
    case GST_VAAPI_PROFILE_H265_MAIN:
      profile_idc = GST_H265_PROFILE_IDC_MAIN;
      break;
    case GST_VAAPI_PROFILE_H265_MAIN10:
      profile_idc = GST_H265_PROFILE_IDC_MAIN_10;
      break;
    case GST_VAAPI_PROFILE_H265_MAIN_STILL_PICTURE:
      profile_idc = GST_H265_PROFILE_IDC_MAIN_STILL_PICTURE;
      break;
    case GST_VAAPI_PROFILE_H265_MAIN_422_10:
      /* Fall through */
    case GST_VAAPI_PROFILE_H265_MAIN_444:
      /* Fall through */
    case GST_VAAPI_PROFILE_H265_MAIN_444_10:
      /* Fall through */
    case GST_VAAPI_PROFILE_H265_MAIN12:
      profile_idc = GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION;
      break;
    case GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN:
      /* Fall through */
    case GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_10:
      /* Fall through */
    case GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444:
      /* Fall through */
    case GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444_10:
      profile_idc = GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING;
      break;
    default:
      GST_DEBUG ("unsupported GstVaapiProfile value");
      profile_idc = 0;
      break;
  }
  return profile_idc;
}

/** Returns GstVaapiProfile from a string representation */
GstVaapiProfile
gst_vaapi_utils_h265_get_profile_from_string (const gchar * str)
{
  const struct map *const m = map_lookup_name (gst_vaapi_h265_profile_map, str);

  return m ? (GstVaapiProfile) m->value : GST_VAAPI_PROFILE_UNKNOWN;
}

/** Returns a string representation for the supplied H.265 profile */
const gchar *
gst_vaapi_utils_h265_get_profile_string (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_h265_profile_map, profile);

  return m ? m->name : NULL;
}

/** Returns GstVaapiLevelH265 from H.265 level_idc value */
GstVaapiLevelH265
gst_vaapi_utils_h265_get_level (guint8 level_idc)
{
  const GstVaapiH265LevelLimits *llp;

  for (llp = gst_vaapi_h265_level_limits; llp->level != 0; llp++) {
    if (llp->level_idc == level_idc)
      return llp->level;
  }
  GST_DEBUG ("unsupported level_idc value");
  return (GstVaapiLevelH265) 0;
}

/** Returns H.265 level_idc value from GstVaapiLevelH265 */
guint8
gst_vaapi_utils_h265_get_level_idc (GstVaapiLevelH265 level)
{
  const GstVaapiH265LevelLimits *const llp =
      gst_vaapi_utils_h265_get_level_limits (level);

  return llp ? llp->level_idc : 0;
}

/** Returns GstVaapiLevelH265 from a string representation */
GstVaapiLevelH265
gst_vaapi_utils_h265_get_level_from_string (const gchar * str)
{
  gint v, level_idc = 0;

  if (!str || !str[0])
    goto not_found;

  v = g_ascii_digit_value (str[0]);
  if (v < 0)
    goto not_found;
  level_idc = v * 30;

  switch (str[1]) {
    case '\0':
      break;
    case '.':
      v = g_ascii_digit_value (str[2]);
      if (v < 0 || str[3] != '\0')
        goto not_found;
      level_idc += v;
      break;
    default:
      goto not_found;
  }
  return gst_vaapi_utils_h265_get_level (level_idc);

not_found:
  return (GstVaapiLevelH265) 0;
}

/** Returns a string representation for the supplied H.265 level */
const gchar *
gst_vaapi_utils_h265_get_level_string (GstVaapiLevelH265 level)
{
  if (level < GST_VAAPI_LEVEL_H265_L1 || level > GST_VAAPI_LEVEL_H265_L6_2)
    return NULL;
  return gst_vaapi_h265_level_map[level - GST_VAAPI_LEVEL_H265_L1].name;
}

/** Returns level limits as specified in Table A-1 of the H.265 standard */
const GstVaapiH265LevelLimits *
gst_vaapi_utils_h265_get_level_limits (GstVaapiLevelH265 level)
{
  if (level < GST_VAAPI_LEVEL_H265_L1 || level > GST_VAAPI_LEVEL_H265_L6_2)
    return NULL;
  return &gst_vaapi_h265_level_limits[level - GST_VAAPI_LEVEL_H265_L1];
}

/** Returns the Table A-1 & A-2 specification */
const GstVaapiH265LevelLimits *
gst_vaapi_utils_h265_get_level_limits_table (guint * out_length_ptr)
{
  if (out_length_ptr)
    *out_length_ptr = G_N_ELEMENTS (gst_vaapi_h265_level_limits) - 1;
  return gst_vaapi_h265_level_limits;
}

/** Returns GstVaapiChromaType from H.265 chroma_format_idc value */
GstVaapiChromaType
gst_vaapi_utils_h265_get_chroma_type (guint chroma_format_idc,
    guint luma_bit_depth, guint chroma_bit_depth)
{
  GstVaapiChromaType chroma_type = (GstVaapiChromaType) 0;
  guint depth = 0;

  if (luma_bit_depth < 8 || chroma_bit_depth < 8 ||
      luma_bit_depth > 16 || chroma_bit_depth > 16) {
    GST_WARNING ("invalid luma_bit_depth or chroma_bit_depth value");
    return chroma_type;
  }

  depth = MAX (luma_bit_depth, chroma_bit_depth);

  switch (chroma_format_idc) {
    case 0:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV400;
      break;
    case 1:
      if (depth == 8)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
      else if (depth > 8 && depth <= 10)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420_10BPP;
      else if (depth > 10 && depth <= 12)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420_12BPP;
      break;
    case 2:
      if (depth == 8)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
      else if (depth > 8 && depth <= 10)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422_10BPP;
      else if (depth > 10 && depth <= 12)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422_12BPP;
      break;
    case 3:
      if (depth == 8)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
      else if (depth > 8 && depth <= 10)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444_10BPP;
      else if (depth > 10 && depth <= 12)
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444_12BPP;
      break;
    default:
      break;
  }

  if (chroma_type == (GstVaapiChromaType) 0)
    GST_DEBUG ("unsupported chroma_format_idc value");

  return chroma_type;
}

/** Returns H.265 chroma_format_idc value from GstVaapiChromaType */
guint
gst_vaapi_utils_h265_get_chroma_format_idc (GstVaapiChromaType chroma_type)
{
  guint chroma_format_idc;

  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV400:
      chroma_format_idc = 0;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV420:
    case GST_VAAPI_CHROMA_TYPE_YUV420_10BPP:
    case GST_VAAPI_CHROMA_TYPE_YUV420_12BPP:
      chroma_format_idc = 1;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
    case GST_VAAPI_CHROMA_TYPE_YUV422_10BPP:
    case GST_VAAPI_CHROMA_TYPE_YUV422_12BPP:
      chroma_format_idc = 2;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
    case GST_VAAPI_CHROMA_TYPE_YUV444_10BPP:
    case GST_VAAPI_CHROMA_TYPE_YUV444_12BPP:
      chroma_format_idc = 3;
      break;
    default:
      GST_DEBUG ("unsupported GstVaapiChromaType value");
      chroma_format_idc = 1;
      break;
  }
  return chroma_format_idc;
}

/** Returns GstVaapiTierH265 from a string representation */
GstVaapiTierH265
gst_vaapi_utils_h265_get_tier_from_string (const gchar * str)
{
  const struct map *const m = map_lookup_name (gst_vaapi_h265_tier_map, str);

  return m ? (GstVaapiTierH265) m->value : GST_VAAPI_TIER_H265_UNKNOWN;
}

/** Returns a string representation for the supplied H.265 tier */
const gchar *
gst_vaapi_utils_h265_get_tier_string (GstVaapiTierH265 tier)
{
  const struct map *const m = map_lookup_value (gst_vaapi_h265_tier_map, tier);

  return m ? m->name : NULL;
}
