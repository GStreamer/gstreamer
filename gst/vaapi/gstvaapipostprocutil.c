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

gboolean
is_deinterlace_enabled (GstVaapiPostproc * postproc, GstVideoInfo * vip)
{
  gboolean deinterlace;

  switch (postproc->deinterlace_mode) {
    case GST_VAAPI_DEINTERLACE_MODE_AUTO:
      deinterlace = GST_VIDEO_INFO_IS_INTERLACED (vip);
      break;
    case GST_VAAPI_DEINTERLACE_MODE_INTERLACED:
      deinterlace = TRUE;
      break;
    default:
      deinterlace = FALSE;
      break;
  }
  return deinterlace;
}

static void
find_best_size (GstVaapiPostproc * postproc, GstVideoInfo * vip,
    guint * width_ptr, guint * height_ptr)
{
  guint width, height;

  width = GST_VIDEO_INFO_WIDTH (vip);
  height = GST_VIDEO_INFO_HEIGHT (vip);
  if (postproc->width && postproc->height) {
    width = postproc->width;
    height = postproc->height;
  } else if (postproc->keep_aspect) {
    const gdouble ratio = (gdouble) width / height;
    if (postproc->width) {
      width = postproc->width;
      height = postproc->width / ratio;
    } else if (postproc->height) {
      height = postproc->height;
      width = postproc->height * ratio;
    }
  } else if (postproc->width)
    width = postproc->width;
  else if (postproc->height)
    height = postproc->height;

  *width_ptr = width;
  *height_ptr = height;
}

/**
 * gst_vaapipostproc_fixate_srccaps:
 * @postproc: a #GstVaapiPostproc instance
 * @sinkcaps: fixed #GstCaps from sink pad
 * @srccaps: #GstCaps from src pad to fixate
 *
 * Given @srccaps and @sinkcaps returns a new allocated #GstCaps with
 * the fixated caps for the src pad.
 *
 * Returns: A new allocated #GstCaps
 **/
GstCaps *
gst_vaapipostproc_fixate_srccaps (GstVaapiPostproc * postproc,
    GstCaps * sinkcaps, GstCaps * srccaps)
{
  GstVideoInfo vi;
  GstVideoFormat out_format;
  GstCaps *out_caps;
  GstVaapiCapsFeature feature;
  const gchar *feature_str;
  guint width, height;
  GstPad *srcpad;

  /* Generate the expected src pad caps, from the current fixated sink
     pad caps */
  if (!gst_video_info_from_caps (&vi, sinkcaps))
    return NULL;

  // Set double framerate in interlaced mode
  if (is_deinterlace_enabled (postproc, &vi)) {
    gint fps_n = GST_VIDEO_INFO_FPS_N (&vi);
    gint fps_d = GST_VIDEO_INFO_FPS_D (&vi);
    if (!gst_util_fraction_multiply (fps_n, fps_d, 2, 1, &fps_n, &fps_d))
      return NULL;
    GST_VIDEO_INFO_FPS_N (&vi) = fps_n;
    GST_VIDEO_INFO_FPS_D (&vi) = fps_d;
  }
  // Signal the other pad that we only generate progressive frames
  GST_VIDEO_INFO_INTERLACE_MODE (&vi) = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  // Update size from user-specified parameters
  find_best_size (postproc, &vi, &width, &height);

  // Update format from user-specified parameters
  srcpad = GST_BASE_TRANSFORM_SRC_PAD (postproc);
  feature = gst_vaapi_find_preferred_caps_feature (srcpad, srccaps,
      &out_format);

  if (postproc->format != DEFAULT_FORMAT)
    out_format = postproc->format;

  if (feature == GST_VAAPI_CAPS_FEATURE_NOT_NEGOTIATED)
    return NULL;

  gst_video_info_change_format (&vi, out_format, width, height);
  out_caps = gst_video_info_to_caps (&vi);
  if (!out_caps)
    return NULL;

  if (feature) {
    feature_str = gst_vaapi_caps_feature_to_string (feature);
    if (feature_str)
      gst_caps_set_features (out_caps, 0,
          gst_caps_features_new (feature_str, NULL));
  }

  /* we don't need to do format conversion if GL_TEXTURE_UPLOAD_META
   * is negotiated */
  if (feature != GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META &&
      postproc->format != out_format) {
    postproc->format = out_format;
  }
  return out_caps;
}
