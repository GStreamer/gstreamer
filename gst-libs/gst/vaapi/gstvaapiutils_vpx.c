/*
 *  gstvaapiutils_vpx.c - vpx related utilities
 *
 *  Copyright (C) 2020 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
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

#include "gstvaapiutils_vpx.h"
#include "gstvaapisurface.h"

struct map
{
  guint value;
  const gchar *name;
};

/* Profile string map */
static const struct map gst_vaapi_vp9_profile_map[] = {
  /* *INDENT-OFF* */
  { GST_VAAPI_PROFILE_VP9_0, "0" },
  { GST_VAAPI_PROFILE_VP9_1, "1" },
  { GST_VAAPI_PROFILE_VP9_2, "2" },
  { GST_VAAPI_PROFILE_VP9_3, "3" },
  { 0, NULL }
  /* *INDENT-ON* */
};

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

/** Returns GstVaapiProfile from a string representation */
GstVaapiProfile
gst_vaapi_utils_vp9_get_profile_from_string (const gchar * str)
{
  const struct map *const m = map_lookup_name (gst_vaapi_vp9_profile_map, str);

  return m ? (GstVaapiProfile) m->value : GST_VAAPI_PROFILE_UNKNOWN;
}

/** Returns a string representation for the supplied VP9 profile */
const gchar *
gst_vaapi_utils_vp9_get_profile_string (GstVaapiProfile profile)
{
  const struct map *const m =
      map_lookup_value (gst_vaapi_vp9_profile_map, profile);

  return m ? m->name : NULL;
}

/** Returns VP9 chroma_format_idc value from GstVaapiChromaType */
guint
gst_vaapi_utils_vp9_get_chroma_format_idc (guint chroma_type)
{
  guint chroma_format_idc;

  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV400:
      chroma_format_idc = 0;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV420:
    case GST_VAAPI_CHROMA_TYPE_YUV420_10BPP:
      chroma_format_idc = 1;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
    case GST_VAAPI_CHROMA_TYPE_YUV422_10BPP:
      chroma_format_idc = 2;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
    case GST_VAAPI_CHROMA_TYPE_YUV444_10BPP:
      chroma_format_idc = 3;
      break;
    default:
      GST_DEBUG ("unsupported GstVaapiChromaType value");
      chroma_format_idc = 1;
      break;
  }
  return chroma_format_idc;
}
