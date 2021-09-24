/*
 *  gstvaapiutils_h264.c - H.264 related utilities
 *
 *  Copyright (C) 2011-2014 Intel Corporation
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
#include "gstvaapicompat.h"
#include "gstvaapiutils_h264_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

struct map
{
  guint value;
  const gchar *name;
};

/* Profile string map */
static const struct map gst_vaapi_h264_profile_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE, "constrained-baseline" },
  { GST_VAAPI_PROFILE_H264_BASELINE,             "baseline"             },
  { GST_VAAPI_PROFILE_H264_MAIN,                 "main"                 },
  { GST_VAAPI_PROFILE_H264_EXTENDED,             "extended"             },
  { GST_VAAPI_PROFILE_H264_HIGH,                 "high"                 },
  { GST_VAAPI_PROFILE_H264_HIGH10,               "high-10"              },
  { GST_VAAPI_PROFILE_H264_HIGH_422,             "high-4:2:2"           },
  { GST_VAAPI_PROFILE_H264_HIGH_444,             "high-4:4:4"           },
  { GST_VAAPI_PROFILE_H264_SCALABLE_BASELINE,    "scalable-baseline"    },
  { GST_VAAPI_PROFILE_H264_SCALABLE_HIGH,        "scalable-high"        },
  { GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH,       "multiview-high"       },
  { GST_VAAPI_PROFILE_H264_STEREO_HIGH,          "stereo-high"          },
  { 0, NULL }
/* *INDENT-ON* */
};

/* Level string map */
static const struct map gst_vaapi_h264_level_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_LEVEL_H264_L1,    "1"     },
  { GST_VAAPI_LEVEL_H264_L1b,   "1b"    },
  { GST_VAAPI_LEVEL_H264_L1_1,  "1.1"   },
  { GST_VAAPI_LEVEL_H264_L1_2,  "1.2"   },
  { GST_VAAPI_LEVEL_H264_L1_3,  "1.3"   },
  { GST_VAAPI_LEVEL_H264_L2,    "2"     },
  { GST_VAAPI_LEVEL_H264_L2_1,  "2.1"   },
  { GST_VAAPI_LEVEL_H264_L2_2,  "2.2"   },
  { GST_VAAPI_LEVEL_H264_L3,    "3"     },
  { GST_VAAPI_LEVEL_H264_L3_1,  "3.1"   },
  { GST_VAAPI_LEVEL_H264_L3_2,  "3.2"   },
  { GST_VAAPI_LEVEL_H264_L4,    "4"     },
  { GST_VAAPI_LEVEL_H264_L4_1,  "4.1"   },
  { GST_VAAPI_LEVEL_H264_L4_2,  "4.2"   },
  { GST_VAAPI_LEVEL_H264_L5,    "5"     },
  { GST_VAAPI_LEVEL_H264_L5_1,  "5.1"   },
  { GST_VAAPI_LEVEL_H264_L5_2,  "5.2"   },
  { GST_VAAPI_LEVEL_H264_L6,    "6"     },
  { GST_VAAPI_LEVEL_H264_L6_1,  "6.1"   },
  { GST_VAAPI_LEVEL_H264_L6_2,  "6.2"   },
  { 0, NULL }
/* *INDENT-ON* */
};

/* Table A-1 - Level limits */
/* *INDENT-OFF* */
static const GstVaapiH264LevelLimits gst_vaapi_h264_level_limits[] = {
  /* level                     idc   MaxMBPS   MaxFS MaxDpbMbs  MaxBR MaxCPB  MinCr */
  { GST_VAAPI_LEVEL_H264_L1,    10,     1485,     99,    396,     64,    175, 2 },
  { GST_VAAPI_LEVEL_H264_L1b,   11,     1485,     99,    396,    128,    350, 2 },
  { GST_VAAPI_LEVEL_H264_L1_1,  11,     3000,    396,    900,    192,    500, 2 },
  { GST_VAAPI_LEVEL_H264_L1_2,  12,     6000,    396,   2376,    384,   1000, 2 },
  { GST_VAAPI_LEVEL_H264_L1_3,  13,    11880,    396,   2376,    768,   2000, 2 },
  { GST_VAAPI_LEVEL_H264_L2,    20,    11880,    396,   2376,   2000,   2000, 2 },
  { GST_VAAPI_LEVEL_H264_L2_1,  21,    19800,    792,   4752,   4000,   4000, 2 },
  { GST_VAAPI_LEVEL_H264_L2_2,  22,    20250,   1620,   8100,   4000,   4000, 2 },
  { GST_VAAPI_LEVEL_H264_L3,    30,    40500,   1620,   8100,  10000,  10000, 2 },
  { GST_VAAPI_LEVEL_H264_L3_1,  31,   108000,   3600,  18000,  14000,  14000, 4 },
  { GST_VAAPI_LEVEL_H264_L3_2,  32,   216000,   5120,  20480,  20000,  20000, 4 },
  { GST_VAAPI_LEVEL_H264_L4,    40,   245760,   8192,  32768,  20000,  25000, 4 },
  { GST_VAAPI_LEVEL_H264_L4_1,  41,   245760,   8192,  32768,  50000,  62500, 2 },
  { GST_VAAPI_LEVEL_H264_L4_2,  42,   522240,   8704,  34816,  50000,  62500, 2 },
  { GST_VAAPI_LEVEL_H264_L5,    50,   589824,  22080, 110400, 135000, 135000, 2 },
  { GST_VAAPI_LEVEL_H264_L5_1,  51,   983040,  36864, 184320, 240000, 240000, 2 },
  { GST_VAAPI_LEVEL_H264_L5_2,  52,  2073600,  36864, 184320, 240000, 240000, 2 },
  { GST_VAAPI_LEVEL_H264_L6,    60,  4177920, 139264, 696320, 240000, 240000, 2 },
  { GST_VAAPI_LEVEL_H264_L6_1,  61,  8355840, 139264, 696320, 480000, 480000, 2 },
  { GST_VAAPI_LEVEL_H264_L6_2,  62, 16711680, 139264, 696320, 800000, 800000, 2 },
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
gst_vaapi_utils_h264_get_profile_score (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_h264_profile_map, profile);

  return m ? 1 + (m - gst_vaapi_h264_profile_map) : 0;
}

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
    case GST_H264_PROFILE_EXTENDED:
      profile = GST_VAAPI_PROFILE_H264_EXTENDED;
      break;
    case GST_H264_PROFILE_HIGH:
      profile = GST_VAAPI_PROFILE_H264_HIGH;
      break;
    case GST_H264_PROFILE_HIGH10:
      profile = GST_VAAPI_PROFILE_H264_HIGH10;
      break;
    case GST_H264_PROFILE_HIGH_422:
      profile = GST_VAAPI_PROFILE_H264_HIGH_422;
      break;
    case GST_H264_PROFILE_HIGH_444:
      profile = GST_VAAPI_PROFILE_H264_HIGH_444;
      break;
    case GST_H264_PROFILE_SCALABLE_BASELINE:
      profile = GST_VAAPI_PROFILE_H264_SCALABLE_BASELINE;
      break;
    case GST_H264_PROFILE_SCALABLE_HIGH:
      profile = GST_VAAPI_PROFILE_H264_SCALABLE_HIGH;
      break;
    case GST_H264_PROFILE_MULTIVIEW_HIGH:
      profile = GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH;
      break;
    case GST_H264_PROFILE_STEREO_HIGH:
      profile = GST_VAAPI_PROFILE_H264_STEREO_HIGH;
      break;
    default:
      GST_DEBUG ("unsupported profile_idc value");
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
    case GST_VAAPI_PROFILE_H264_EXTENDED:
      profile_idc = GST_H264_PROFILE_EXTENDED;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH:
      profile_idc = GST_H264_PROFILE_HIGH;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH10:
      profile_idc = GST_H264_PROFILE_HIGH10;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH_422:
      profile_idc = GST_H264_PROFILE_HIGH_422;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH_444:
      profile_idc = GST_H264_PROFILE_HIGH_444;
      break;
    case GST_VAAPI_PROFILE_H264_SCALABLE_BASELINE:
      profile_idc = GST_H264_PROFILE_SCALABLE_BASELINE;
      break;
    case GST_VAAPI_PROFILE_H264_SCALABLE_HIGH:
      profile_idc = GST_H264_PROFILE_SCALABLE_HIGH;
      break;
    case GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH:
      profile_idc = GST_H264_PROFILE_MULTIVIEW_HIGH;
      break;
    case GST_VAAPI_PROFILE_H264_STEREO_HIGH:
      profile_idc = GST_H264_PROFILE_STEREO_HIGH;
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
gst_vaapi_utils_h264_get_profile_from_string (const gchar * str)
{
  const struct map *const m = map_lookup_name (gst_vaapi_h264_profile_map, str);

  return m ? (GstVaapiProfile) m->value : GST_VAAPI_PROFILE_UNKNOWN;
}

/** Returns a string representation for the supplied H.264 profile */
const gchar *
gst_vaapi_utils_h264_get_profile_string (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_h264_profile_map, profile);

  return m ? m->name : NULL;
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
  GST_DEBUG ("unsupported level_idc value");
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

/** Returns GstVaapiLevelH264 from a string representation */
GstVaapiLevelH264
gst_vaapi_utils_h264_get_level_from_string (const gchar * str)
{
  gint v, level_idc = 0;

  if (!str || !str[0])
    goto not_found;

  v = g_ascii_digit_value (str[0]);
  if (v < 0)
    goto not_found;
  level_idc = v * 10;

  switch (str[1]) {
    case '\0':
      break;
    case '.':
      v = g_ascii_digit_value (str[2]);
      if (v < 0 || str[3] != '\0')
        goto not_found;
      level_idc += v;
      break;
    case 'b':
      if (level_idc == 10 && str[2] == '\0')
        return GST_VAAPI_LEVEL_H264_L1b;
      // fall-trough
    default:
      goto not_found;
  }
  return gst_vaapi_utils_h264_get_level (level_idc);

not_found:
  return (GstVaapiLevelH264) 0;
}

/** Returns a string representation for the supplied H.264 level */
const gchar *
gst_vaapi_utils_h264_get_level_string (GstVaapiLevelH264 level)
{
  if (level < GST_VAAPI_LEVEL_H264_L1 || level > GST_VAAPI_LEVEL_H264_L6_2)
    return NULL;
  return gst_vaapi_h264_level_map[level - GST_VAAPI_LEVEL_H264_L1].name;
}

/** Returns level limits as specified in Table A-1 of the H.264 standard */
const GstVaapiH264LevelLimits *
gst_vaapi_utils_h264_get_level_limits (GstVaapiLevelH264 level)
{
  if (level < GST_VAAPI_LEVEL_H264_L1 || level > GST_VAAPI_LEVEL_H264_L6_2)
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
      GST_DEBUG ("unsupported chroma_format_idc value");
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
      GST_DEBUG ("unsupported GstVaapiChromaType value");
      chroma_format_idc = 1;
      break;
  }
  return chroma_format_idc;
}
