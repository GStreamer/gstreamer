/*
 *  gstvaapiutils_mpeg2.c - MPEG-2 related utilities
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
#include <gst/codecparsers/gstmpegvideoparser.h>
#include "gstvaapicompat.h"
#include "gstvaapiutils_mpeg2_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

struct map
{
  guint value;
  const gchar *name;
};

/* Profile string map */
static const struct map gst_vaapi_mpeg2_profile_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_PROFILE_MPEG2_SIMPLE,     "simple"        },
  { GST_VAAPI_PROFILE_MPEG2_MAIN,       "main"          },
  { GST_VAAPI_PROFILE_MPEG2_HIGH,       "high"          },
  { 0, NULL }
/* *INDENT-ON* */
};

/* Level string map */
static const struct map gst_vaapi_mpeg2_level_map[] = {
/* *INDENT-OFF* */
  { GST_VAAPI_LEVEL_MPEG2_LOW,          "low"           },
  { GST_VAAPI_LEVEL_MPEG2_MAIN,         "main"          },
  { GST_VAAPI_LEVEL_MPEG2_HIGH_1440,    "high-1440"     },
  { GST_VAAPI_LEVEL_MPEG2_HIGH,         "high"          },
  { GST_VAAPI_LEVEL_MPEG2_HIGHP,        "highP"         },
  { 0, NULL }
/* *INDENT-ON* */
};

/* Table 8-10 to 8-13 (up to Main profile only) */
/* *INDENT-OFF* */
static const GstVaapiMPEG2LevelLimits gst_vaapi_mpeg2_level_limits[] = {
  /* level      h_size  v_size  fps  samples     kbps  vbv_size */
  { GST_VAAPI_LEVEL_MPEG2_LOW,
    0x0a,        352,   288,   30,   3041280,   4000,   475136 },
  { GST_VAAPI_LEVEL_MPEG2_MAIN,
    0x08,        720,   576,   30,   1036800,  15000,  1835008 },
  { GST_VAAPI_LEVEL_MPEG2_HIGH_1440,
    0x06,       1440,  1152,   60,  47001600,  60000,  7340032 },
  { GST_VAAPI_LEVEL_MPEG2_HIGH,
    0x04,       1920,  1152,   60,  62668800,  80000,  9781248 },
  /* Amendment 3: New level for 1080@50p/60p */
  { GST_VAAPI_LEVEL_MPEG2_HIGHP,
    0x02,       1920,  1152,   60, 125337600,  80000,  9781248 },
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
gst_vaapi_utils_mpeg2_get_profile_score (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_mpeg2_profile_map, profile);

  return m ? 1 + (m - gst_vaapi_mpeg2_profile_map) : 0;
}

/** Returns GstVaapiProfile from MPEG-2 profile_idc value */
GstVaapiProfile
gst_vaapi_utils_mpeg2_get_profile (guint8 profile_idc)
{
  GstVaapiProfile profile;

  switch (profile_idc) {
    case GST_MPEG_VIDEO_PROFILE_SIMPLE:
      profile = GST_VAAPI_PROFILE_MPEG2_SIMPLE;
      break;
    case GST_MPEG_VIDEO_PROFILE_MAIN:
      profile = GST_VAAPI_PROFILE_MPEG2_MAIN;
      break;
    case GST_MPEG_VIDEO_PROFILE_HIGH:
      profile = GST_VAAPI_PROFILE_MPEG2_HIGH;
      break;
    default:
      GST_DEBUG ("unsupported profile_idc value");
      profile = GST_VAAPI_PROFILE_UNKNOWN;
      break;
  }
  return profile;
}

/** Returns MPEG-2 profile_idc value from GstVaapiProfile */
guint8
gst_vaapi_utils_mpeg2_get_profile_idc (GstVaapiProfile profile)
{
  guint8 profile_idc;

  switch (profile) {
    case GST_VAAPI_PROFILE_MPEG2_SIMPLE:
      profile_idc = GST_MPEG_VIDEO_PROFILE_SIMPLE;
      break;
    case GST_VAAPI_PROFILE_MPEG2_MAIN:
      profile_idc = GST_MPEG_VIDEO_PROFILE_MAIN;
      break;
    case GST_VAAPI_PROFILE_MPEG2_HIGH:
      profile_idc = GST_MPEG_VIDEO_PROFILE_HIGH;
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
gst_vaapi_utils_mpeg2_get_profile_from_string (const gchar * str)
{
  const struct map *const m =
      map_lookup_name (gst_vaapi_mpeg2_profile_map, str);

  return m ? (GstVaapiProfile) m->value : GST_VAAPI_PROFILE_UNKNOWN;
}

/** Returns a string representation for the supplied MPEG-2 profile */
const gchar *
gst_vaapi_utils_mpeg2_get_profile_string (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_mpeg2_profile_map, profile);

  return m ? m->name : NULL;
}

/** Returns GstVaapiLevelMPEG2 from MPEG-2 level_idc value */
GstVaapiLevelMPEG2
gst_vaapi_utils_mpeg2_get_level (guint8 level_idc)
{
  const GstVaapiMPEG2LevelLimits *llp;

  for (llp = gst_vaapi_mpeg2_level_limits; llp->level != 0; llp++) {
    if (llp->level_idc == level_idc)
      return llp->level;
  }
  GST_DEBUG ("unsupported level_idc value");
  return (GstVaapiLevelMPEG2) 0;
}

/** Returns MPEG-2 level_idc value from GstVaapiLevelMPEG2 */
guint8
gst_vaapi_utils_mpeg2_get_level_idc (GstVaapiLevelMPEG2 level)
{
  const GstVaapiMPEG2LevelLimits *const llp =
      gst_vaapi_utils_mpeg2_get_level_limits (level);

  return llp ? llp->level_idc : 0;
}

/** Returns GstVaapiLevelMPEG2 from a string representation */
GstVaapiLevelMPEG2
gst_vaapi_utils_mpeg2_get_level_from_string (const gchar * str)
{
  const struct map *const m = map_lookup_name (gst_vaapi_mpeg2_level_map, str);

  return (GstVaapiLevelMPEG2) (m ? m->value : 0);
}

/** Returns a string representation for the supplied MPEG-2 level */
const gchar *
gst_vaapi_utils_mpeg2_get_level_string (GstVaapiLevelMPEG2 level)
{
  if (level < GST_VAAPI_LEVEL_MPEG2_LOW || level > GST_VAAPI_LEVEL_MPEG2_HIGHP)
    return NULL;
  return gst_vaapi_mpeg2_level_map[level - GST_VAAPI_LEVEL_MPEG2_LOW].name;
}

/** Returns level limits as specified in Tables 8-10 to 8-13 of the
    MPEG-2 standard */
const GstVaapiMPEG2LevelLimits *
gst_vaapi_utils_mpeg2_get_level_limits (GstVaapiLevelMPEG2 level)
{
  if (level < GST_VAAPI_LEVEL_MPEG2_LOW || level > GST_VAAPI_LEVEL_MPEG2_HIGHP)
    return NULL;
  return &gst_vaapi_mpeg2_level_limits[level - GST_VAAPI_LEVEL_MPEG2_LOW];
}

/** Returns Tables 8-10 to 8-13 from the specification (up to High profile) */
const GstVaapiMPEG2LevelLimits *
gst_vaapi_utils_mpeg2_get_level_limits_table (guint * out_length_ptr)
{
  if (out_length_ptr)
    *out_length_ptr = G_N_ELEMENTS (gst_vaapi_mpeg2_level_limits) - 1;
  return gst_vaapi_mpeg2_level_limits;
}

/** Returns GstVaapiChromaType from MPEG-2 chroma_format_idc value */
GstVaapiChromaType
gst_vaapi_utils_mpeg2_get_chroma_type (guint chroma_format_idc)
{
  GstVaapiChromaType chroma_type;

  switch (chroma_format_idc) {
    case GST_MPEG_VIDEO_CHROMA_420:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
      break;
    case GST_MPEG_VIDEO_CHROMA_422:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
      break;
    case GST_MPEG_VIDEO_CHROMA_444:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
      break;
    default:
      GST_DEBUG ("unsupported chroma_format_idc value");
      chroma_type = (GstVaapiChromaType) 0;
      break;
  }
  return chroma_type;
}

/** Returns MPEG-2 chroma_format_idc value from GstVaapiChromaType */
guint
gst_vaapi_utils_mpeg2_get_chroma_format_idc (GstVaapiChromaType chroma_type)
{
  guint chroma_format_idc;

  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV420:
      chroma_format_idc = GST_MPEG_VIDEO_CHROMA_420;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
      chroma_format_idc = GST_MPEG_VIDEO_CHROMA_422;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
      chroma_format_idc = GST_MPEG_VIDEO_CHROMA_444;
      break;
    default:
      GST_DEBUG ("unsupported GstVaapiChromaType value");
      chroma_format_idc = 1;
      break;
  }
  return chroma_format_idc;
}
