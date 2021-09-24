/*
 *  gstvaapipostprocutil.h - VA-API video post processing utilities
 *
 *  Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *  Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
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

#include <gst/vaapi/gstvaapifilter.h>

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
  g_value_unset (&value);
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

static gboolean
_fixate_frame_size (GstVaapiPostproc * postproc, GstVideoInfo * vinfo,
    GstStructure * outs)
{
  const GValue *to_par;
  GValue tpar = G_VALUE_INIT;
  gboolean ret;

  ret = TRUE;
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");
  if (!to_par) {
    g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
    gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
    to_par = &tpar;
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    from_par_n = GST_VIDEO_INFO_PAR_N (vinfo);
    from_par_d = GST_VIDEO_INFO_PAR_D (vinfo);
    from_w = GST_VIDEO_INFO_WIDTH (vinfo);
    from_h = GST_VIDEO_INFO_HEIGHT (vinfo);

    if (postproc->has_vpp) {
      /* adjust for crop settings */
      from_w -= postproc->crop_left + postproc->crop_right;
      from_h -= postproc->crop_top + postproc->crop_bottom;

      /* compensate for rotation if needed */
      switch (gst_vaapi_filter_get_video_direction (postproc->filter)) {
        case GST_VIDEO_ORIENTATION_90R:
        case GST_VIDEO_ORIENTATION_90L:
        case GST_VIDEO_ORIENTATION_UL_LR:
        case GST_VIDEO_ORIENTATION_UR_LL:
          G_PRIMITIVE_SWAP (gint, from_w, from_h);
          G_PRIMITIVE_SWAP (gint, from_par_n, from_par_d);
        default:
          break;
      }
    }

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (postproc,
          "dimensions already set to %dx%d, not fixating", w, h);

      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (postproc, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }

      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d))
      goto overflow_error;

    GST_DEBUG_OBJECT (postproc, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (postproc, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (postproc, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den))
          goto overflow_error;

        w = (guint) gst_util_uint64_scale_int (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        gst_structure_free (tmp);
        goto overflow_error;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den))
        goto overflow_error;

      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (postproc, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (postproc, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den))
          goto overflow_error;

        h = (guint) gst_util_uint64_scale_int (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        gst_structure_free (tmp);
        goto overflow_error;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den))
        goto overflow_error;

      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den))
        goto overflow_error;

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the height that was nearest to the orignal
       * height and the nearest possible width. This changes the DAR but
       * there's not much else to do here.
       */
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        gst_structure_free (tmp);
        goto overflow_error;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den))
        goto overflow_error;

      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return ret;

  /* ERRORS */
overflow_error:
  {
    ret = FALSE;
    GST_ELEMENT_ERROR (postproc, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output scaled size - integer overflow"));
    goto done;
  }
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
_set_multiview_mode (GstVaapiPostproc * postproc, GstVideoInfo * vinfo,
    GstStructure * outs)
{
  const gchar *caps_str;

  caps_str =
      gst_video_multiview_mode_to_caps_string (GST_VIDEO_INFO_MULTIVIEW_MODE
      (vinfo));
  if (!caps_str)
    return TRUE;

  gst_structure_set (outs, "multiview-mode", G_TYPE_STRING, caps_str,
      "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
      GST_VIDEO_INFO_MULTIVIEW_FLAGS (vinfo), GST_FLAG_SET_MASK_EXACT, NULL);

  if (GST_VIDEO_INFO_VIEWS (vinfo) > 1) {
    gst_structure_set (outs, "views", G_TYPE_INT, GST_VIDEO_INFO_VIEWS (vinfo),
        NULL);
  }

  return TRUE;
}

static gboolean
_set_colorimetry (GstVaapiPostproc * postproc, GstVideoInfo * sinkinfo,
    GstVideoFormat format, GstStructure * outs)
{
  GstVideoInfo vinfo;
  GstVideoColorimetry colorimetry;
  gchar *color;
  gint width, height;

  if (!gst_structure_get_int (outs, "width", &width)
      || !gst_structure_get_int (outs, "height", &height))
    return FALSE;

  /* Use the sink resolution and the src format to correctly determine the
   * default src colorimetry. */
  gst_video_info_set_format (&vinfo, format, GST_VIDEO_INFO_WIDTH (sinkinfo),
      GST_VIDEO_INFO_HEIGHT (sinkinfo));

  if (GST_VIDEO_INFO_CHROMA_SITE (&vinfo) != GST_VIDEO_CHROMA_SITE_UNKNOWN) {
    gst_structure_set (outs, "chroma-site", G_TYPE_STRING,
        gst_video_chroma_to_string (GST_VIDEO_INFO_CHROMA_SITE (&vinfo)), NULL);
  }

  /* if outs structure already specifies colorimetry, use it */
  if (gst_structure_has_field (outs, "colorimetry"))
    return TRUE;

  /* make sure we set the RGB matrix for RGB formats */
  colorimetry = GST_VIDEO_INFO_COLORIMETRY (&vinfo);
  if (GST_VIDEO_FORMAT_INFO_IS_RGB (vinfo.finfo) &&
      colorimetry.matrix != GST_VIDEO_COLOR_MATRIX_RGB) {
    GST_WARNING ("invalid matrix %d for RGB format, using RGB",
        colorimetry.matrix);
    colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
  }

  if ((color = gst_video_colorimetry_to_string (&colorimetry))) {
    gst_structure_set (outs, "colorimetry", G_TYPE_STRING, color, NULL);
    g_free (color);
  }

  return TRUE;
}

static gboolean
_set_interlace_mode (GstVaapiPostproc * postproc, GstVideoInfo * vinfo,
    GstStructure * outs)
{
  const gchar *interlace_mode = NULL;

  if (is_deinterlace_enabled (postproc, vinfo)) {
    interlace_mode = "progressive";
  } else {
    interlace_mode =
        gst_video_interlace_mode_to_string (GST_VIDEO_INFO_INTERLACE_MODE
        (vinfo));
  }

  if (!interlace_mode)
    return FALSE;

  gst_structure_set (outs, "interlace-mode", G_TYPE_STRING, interlace_mode,
      NULL);
  return TRUE;
}

static gboolean
_set_preferred_format (GstStructure * outs, GstVideoFormat format)
{
  GValue value = G_VALUE_INIT;

  if (format == GST_VIDEO_FORMAT_UNKNOWN || format == GST_VIDEO_FORMAT_ENCODED)
    return FALSE;

  if (!gst_vaapi_value_set_format (&value, format))
    return FALSE;
  gst_structure_set_value (outs, "format", &value);
  g_value_unset (&value);
  return TRUE;
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
  _set_multiview_mode (postproc, vinfo, structure);

  if (!_set_colorimetry (postproc, vinfo, format, structure))
    goto fixate_failed;

  if (!_set_interlace_mode (postproc, vinfo, structure))
    goto interlace_mode_failed;

  outcaps = gst_caps_new_empty ();
  gst_caps_append_structure_full (outcaps, structure,
      gst_caps_features_copy (features));

  /* we don't need to do format conversion if GL_TEXTURE_UPLOAD_META
   * is negotiated */
  if (f == GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META) {
    postproc->format = DEFAULT_FORMAT;
  } else if (postproc->format != format) {
    postproc->format = format;
  }

  return gst_caps_fixate (outcaps);

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
interlace_mode_failed:
  {
    GST_WARNING_OBJECT (postproc, "Invalid sink caps interlace mode");
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
