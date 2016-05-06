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

#define GST_CAT_DEFAULT (GST_VAAPI_PLUGIN_BASE (postproc)->debug_category)

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

static gboolean
_fixate_frame_size (GstVaapiPostproc * postproc, GstVideoInfo * vinfo,
    GstStructure * outs)
{
  guint width, height, par_n, par_d;

  par_n = GST_VIDEO_INFO_PAR_N (vinfo);
  par_d = GST_VIDEO_INFO_PAR_D (vinfo);
  find_best_size (postproc, vinfo, &width, &height);
  gst_structure_set (outs, "width", G_TYPE_INT, width, "height", G_TYPE_INT,
      height, "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
  return TRUE;
}

static gboolean
_fixate_frame_rate (GstVaapiPostproc * postproc, GstVideoInfo * vinfo,
    GstStructure * outs)
{
  gint fps_n, fps_d;

  fps_n = GST_VIDEO_INFO_FPS_N (vinfo);
  fps_d = GST_VIDEO_INFO_FPS_D (vinfo);
  if (is_deinterlace_enabled (postproc, vinfo)) {
    if (!gst_util_fraction_multiply (fps_n, fps_d, 2, 1, &fps_n, &fps_d))
      goto overflow_error;
  }
  gst_structure_set (outs, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
  return TRUE;

  /* ERRORS */
overflow_error:
  {
    GST_ELEMENT_ERROR (postproc, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output framerate - integer overflow"));
    return FALSE;
  }
}

static gboolean
_set_preferred_format (GstStructure * outs, GstVideoFormat format)
{
  GValue value = G_VALUE_INIT;

  if (format == GST_VIDEO_FORMAT_UNKNOWN || format == GST_VIDEO_FORMAT_ENCODED)
    return FALSE;

  if (gst_vaapi_value_set_format (&value, format)) {
    gst_structure_set_value (outs, "format", &value);
    return TRUE;
  }
  return FALSE;
}

static GstCaps *
_get_preferred_caps (GstVaapiPostproc * postproc, GstVideoInfo * vinfo,
    GstCaps * srccaps)
{
  GstPad *srcpad;
  GstVideoFormat format;
  GstVaapiCapsFeature f;
  const gchar *feature;
  GstStructure *structure;
  GstCapsFeatures *features;
  GstCaps *outcaps;
  gint i, n;

  format = GST_VIDEO_FORMAT_UNKNOWN;
  srcpad = GST_BASE_TRANSFORM_SRC_PAD (postproc);
  f = gst_vaapi_find_preferred_caps_feature (srcpad, srccaps, &format);
  if (f == GST_VAAPI_CAPS_FEATURE_NOT_NEGOTIATED)
    return NULL;

  feature = gst_vaapi_caps_feature_to_string (f);
  if (!feature)
    feature = GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY;

  n = gst_caps_get_size (srccaps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (srccaps, i);
    features = gst_caps_get_features (srccaps, i);

    if (!gst_caps_features_is_any (features)
        && gst_caps_features_contains (features, feature))
      break;
  }

  if (i >= n)
    goto invalid_caps;

  /* make copy */
  structure = gst_structure_copy (structure);

  if (!_set_preferred_format (structure, format))
    goto fixate_failed;
  if (!_fixate_frame_size (postproc, vinfo, structure))
    goto fixate_failed;
  if (!_fixate_frame_rate (postproc, vinfo, structure))
    goto fixate_failed;

  outcaps = gst_caps_new_empty ();
  gst_caps_append_structure_full (outcaps, structure,
      gst_caps_features_copy (features));

  /* we don't need to do format conversion if GL_TEXTURE_UPLOAD_META
   * is negotiated */
  if (f != GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META
      && postproc->format != format)
    postproc->format = format;

  return outcaps;

  /* ERRORS */
fixate_failed:
  {
    GST_WARNING_OBJECT (postproc, "Could not fixate src caps");
    gst_structure_free (structure);
    return NULL;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (postproc, "No valid src caps found");
    return NULL;
  }
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

  if (!gst_video_info_from_caps (&vi, sinkcaps))
    return NULL;
  return _get_preferred_caps (postproc, &vi, srccaps);
}
