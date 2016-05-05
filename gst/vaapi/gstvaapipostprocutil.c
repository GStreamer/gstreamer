/*
 *  gstvaapipostprocutil.h - VA-API video post processing utilities
 *
 *  Copyright (C) 2016 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *    Author: Victor Jaquez <victorx.jaquez@intel.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapipostprocutil.h"
#include "gstvaapipluginutil.h"

/* if format property is set */
static void
_transform_format (GstVaapiPostproc * postproc, GstCapsFeatures * features,
    GstStructure * structure)
{
  GValue value = G_VALUE_INIT;

  if (postproc->format == DEFAULT_FORMAT)
    return;

  if (!gst_caps_features_is_equal (features,
          GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)
      && !gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE))
    return;

  if (!gst_vaapi_value_set_format (&value, postproc->format))
    return;

  gst_structure_set_value (structure, "format", &value);
}

static void
_set_int (GValue * value, gint val)
{
  g_value_init (value, G_TYPE_INT);
  g_value_set_int (value, val);
}

static void
_set_int_range (GValue * value)
{
  g_value_init (value, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (value, 1, G_MAXINT);
}

static void
_transform_frame_size (GstVaapiPostproc * postproc, GstStructure * structure)
{
  GValue width = G_VALUE_INIT;
  GValue height = G_VALUE_INIT;

  if (postproc->width && postproc->height) {
    _set_int (&width, postproc->width);
    _set_int (&height, postproc->height);
  } else if (postproc->width) {
    _set_int (&width, postproc->width);
    _set_int_range (&height);
  } else if (postproc->height) {
    _set_int_range (&width);
    _set_int (&height, postproc->height);
  } else {
    _set_int_range (&width);
    _set_int_range (&height);
  }

  gst_structure_set_value (structure, "width", &width);
  gst_structure_set_value (structure, "height", &height);
}

/**
 * gst_vaapipostproc_transform_srccaps:
 * @postproc: a #GstVaapiPostproc instance
 *
 * Early apply transformation of the src pad caps according to the set
 * properties.
 *
 * Returns: A new allocated #GstCaps
 **/
GstCaps *
gst_vaapipostproc_transform_srccaps (GstVaapiPostproc * postproc)
{
  GstCaps *out_caps;
  GstStructure *structure;
  GstCapsFeatures *features;
  gint i, n;

  out_caps = gst_caps_new_empty ();
  n = gst_caps_get_size (postproc->allowed_srcpad_caps);

  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (postproc->allowed_srcpad_caps, i);
    features = gst_caps_get_features (postproc->allowed_srcpad_caps, i);

    /* make copy */
    structure = gst_structure_copy (structure);

    if (postproc->keep_aspect)
      gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1,
          1, NULL);

    _transform_format (postproc, features, structure);
    _transform_frame_size (postproc, structure);

    gst_caps_append_structure_full (out_caps, structure,
        gst_caps_features_copy (features));
  }

  return out_caps;
}
