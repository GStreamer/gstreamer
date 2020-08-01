/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvacaps.h"

#include <va/va_drmcommon.h>

#include "gstvadisplay.h"
#include "gstvaprofile.h"
#include "gstvavideoformat.h"

GST_DEBUG_CATEGORY_EXTERN (gst_va_display_debug);
#define GST_CAT_DEFAULT gst_va_display_debug

static const guint va_rt_format_list[] = {
#define R(name) G_PASTE (VA_RT_FORMAT_, name)
  R (YUV420),
  R (YUV422),
  R (YUV444),
  R (YUV411),
  R (YUV400),
  R (YUV420_10),
  R (YUV422_10),
  R (YUV444_10),
  R (YUV420_12),
  R (YUV422_12),
  R (YUV444_12),
  R (YUV420_10BPP),
  R (RGB16),
  R (RGB32),
  R (RGBP),
  R (RGB32_10),
  R (RGB32_10BPP),
  R (PROTECTED),
#undef R
};

VASurfaceAttrib *
gst_va_get_surface_attribs (GstVaDisplay * display, VAConfigID config,
    guint * attrib_count)
{
  VADisplay dpy;
  VASurfaceAttrib *attribs;
  VAStatus status;

  dpy = gst_va_display_get_va_dpy (display);

  gst_va_display_lock (display);
  status = vaQuerySurfaceAttributes (dpy, config, NULL, attrib_count);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (display, "vaQuerySurfaceAttributes: %s",
        vaErrorStr (status));
    return NULL;
  }

  attribs = g_new (VASurfaceAttrib, *attrib_count);

  gst_va_display_lock (display);
  status = vaQuerySurfaceAttributes (dpy, config, attribs, attrib_count);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (display, "vaQuerySurfaceAttributes: %s",
        vaErrorStr (status));
    goto bail;
  }

  return attribs;

bail:
  g_free (attribs);
  return NULL;
}

static gboolean
_gst_caps_set_format_array (GstCaps * caps, GArray * formats)
{
  GstVideoFormat fmt;
  GValue v_formats = G_VALUE_INIT;
  const gchar *format;
  guint i;

  if (formats->len == 1) {
    fmt = g_array_index (formats, GstVideoFormat, 0);
    if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
      return FALSE;
    format = gst_video_format_to_string (fmt);
    if (!format)
      return FALSE;

    g_value_init (&v_formats, G_TYPE_STRING);
    g_value_set_string (&v_formats, format);
  } else if (formats->len > 1) {

    gst_value_list_init (&v_formats, formats->len);

    for (i = 0; i < formats->len; i++) {
      GValue item = G_VALUE_INIT;

      fmt = g_array_index (formats, GstVideoFormat, i);
      if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
        continue;
      format = gst_video_format_to_string (fmt);
      if (!format)
        continue;

      g_value_init (&item, G_TYPE_STRING);
      g_value_set_string (&item, format);
      gst_value_list_append_value (&v_formats, &item);
      g_value_unset (&item);
    }
  } else {
    return FALSE;
  }

  gst_caps_set_value (caps, "format", &v_formats);
  g_value_unset (&v_formats);

  return TRUE;
}

GstCaps *
gst_va_create_raw_caps_from_config (GstVaDisplay * display, VAConfigID config)
{
  GArray *formats;
  GstCaps *caps, *base_caps, *feature_caps;
  GstCapsFeatures *features;
  GstVideoFormat format;
  VASurfaceAttrib *attribs;
  guint i, attrib_count, mem_type = 0;
  gint min_width = 1, max_width = G_MAXINT;
  gint min_height = 1, max_height = G_MAXINT;

  attribs = gst_va_get_surface_attribs (display, config, &attrib_count);
  if (!attribs)
    return NULL;
  formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));

  for (i = 0; i < attrib_count; i++) {
    if (attribs[i].value.type != VAGenericValueTypeInteger)
      continue;
    switch (attribs[i].type) {
      case VASurfaceAttribPixelFormat:
        format = gst_va_video_format_from_va_fourcc (attribs[i].value.value.i);
        if (format != GST_VIDEO_FORMAT_UNKNOWN)
          g_array_append_val (formats, format);
        break;
      case VASurfaceAttribMinWidth:
        min_width = MAX (min_width, attribs[i].value.value.i);
        break;
      case VASurfaceAttribMaxWidth:
        max_width = attribs[i].value.value.i;
        break;
      case VASurfaceAttribMinHeight:
        min_height = MAX (min_height, attribs[i].value.value.i);
        break;
      case VASurfaceAttribMaxHeight:
        max_height = attribs[i].value.value.i;
        break;
      case VASurfaceAttribMemoryType:
        mem_type = attribs[i].value.value.i;
        break;
      default:
        break;
    }
  }

  /* if driver doesn't report surface formats for current
   * chroma. Gallium AMD bug for 4:2:2 */
  if (formats->len == 0) {
    caps = NULL;
    goto bail;
  }

  base_caps = gst_caps_new_simple ("video/x-raw", "width", GST_TYPE_INT_RANGE,
      min_width, max_width, "height", GST_TYPE_INT_RANGE, min_height,
      max_height, NULL);

  _gst_caps_set_format_array (base_caps, formats);

  caps = gst_caps_new_empty ();

  if (mem_type & VA_SURFACE_ATTRIB_MEM_TYPE_VA) {
    feature_caps = gst_caps_copy (base_caps);
    features = gst_caps_features_from_string ("memory:VAMemory");
    gst_caps_set_features_simple (feature_caps, features);
    caps = gst_caps_merge (caps, feature_caps);
  }
  if (mem_type & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME
      || mem_type & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2) {
    feature_caps = gst_caps_copy (base_caps);
    features = gst_caps_features_from_string ("memory:DMABuf");
    gst_caps_set_features_simple (feature_caps, features);
    caps = gst_caps_merge (caps, feature_caps);
  }
  /* raw caps */
  /* XXX(victor): assumption -- drivers can only download to image
   * formats with same chroma of surface's format
   */
  {
    GstCaps *raw_caps;
    GArray *image_formats = gst_va_display_get_image_formats (display);

    if (!image_formats) {
      raw_caps = gst_caps_copy (base_caps);
    } else {
      GArray *raw_formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));
      guint j, surface_chroma, image_chroma;
      GstVideoFormat image_format;

      raw_caps =
          gst_caps_new_simple ("video/x-raw", "width", GST_TYPE_INT_RANGE,
          min_width, max_width, "height", GST_TYPE_INT_RANGE, min_height,
          max_height, NULL);

      for (i = 0; i < formats->len; i++) {
        format = g_array_index (formats, GstVideoFormat, i);
        surface_chroma = gst_va_chroma_from_video_format (format);
        if (surface_chroma == 0)
          continue;

        g_array_append_val (raw_formats, format);

        for (j = 0; j < image_formats->len; j++) {
          image_format = g_array_index (image_formats, GstVideoFormat, j);
          image_chroma = gst_va_chroma_from_video_format (image_format);
          if (image_format != format && surface_chroma == image_chroma)
            g_array_append_val (raw_formats, image_format);
        }
      }

      if (!_gst_caps_set_format_array (raw_caps, raw_formats)) {
        gst_caps_unref (raw_caps);
        raw_caps = gst_caps_copy (base_caps);
      }

      g_array_unref (raw_formats);
      g_array_unref (image_formats);
    }

    caps = gst_caps_merge (caps, raw_caps);
  }

  gst_caps_unref (base_caps);

bail:
  g_array_unref (formats);
  g_free (attribs);

  return caps;
}

static GstCaps *
gst_va_create_raw_caps (GstVaDisplay * display, VAProfile profile,
    VAEntrypoint entrypoint, guint rt_format)
{
  GstCaps *caps;
  VAConfigAttrib attrib = {
    .type = VAConfigAttribRTFormat,
    .value = rt_format,
  };
  VAConfigID config;
  VADisplay dpy;
  VAStatus status;

  dpy = gst_va_display_get_va_dpy (display);

  gst_va_display_lock (display);
  status = vaCreateConfig (dpy, profile, entrypoint, &attrib, 1, &config);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (display, "vaCreateConfig: %s", vaErrorStr (status));
    return NULL;
  }

  caps = gst_va_create_raw_caps_from_config (display, config);

  gst_va_display_lock (display);
  status = vaDestroyConfig (dpy, config);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (display, "vaDestroyConfig: %s", vaErrorStr (status));
    return NULL;
  }

  return caps;
}

static GstCaps *
gst_va_create_coded_caps (GstVaDisplay * display, VAProfile profile,
    VAEntrypoint entrypoint, guint32 * rt_formats_ptr)
{
  GstCaps *caps;
  VAConfigAttrib attribs[] = {
    {.type = VAConfigAttribMaxPictureWidth,},
    {.type = VAConfigAttribMaxPictureHeight,},
    {.type = VAConfigAttribRTFormat,},
  };
  VADisplay dpy;
  VAStatus status;
  guint32 value, rt_formats = 0;
  gint i, max_width = -1, max_height = -1;

  dpy = gst_va_display_get_va_dpy (display);

  gst_va_display_lock (display);
  status = vaGetConfigAttributes (dpy, profile, entrypoint, attribs,
      G_N_ELEMENTS (attribs));
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (display, "vaGetConfigAttributes: %s",
        vaErrorStr (status));
    return NULL;
  }

  for (i = 0; i < G_N_ELEMENTS (attribs); i++) {
    value = attribs[i].value;
    if (value == VA_ATTRIB_NOT_SUPPORTED)
      continue;
    switch (attribs[i].type) {
      case VAConfigAttribMaxPictureHeight:
        if (value <= G_MAXINT)
          max_height = value;
        break;
      case VAConfigAttribMaxPictureWidth:
        if (value <= G_MAXINT)
          max_width = value;
        break;
      case VAConfigAttribRTFormat:
        rt_formats = value;
        break;
      default:
        break;
    }
  }

  if (rt_formats_ptr)
    *rt_formats_ptr = rt_formats;

  caps = gst_va_profile_caps (profile);
  if (!caps)
    return NULL;

  if (max_width == -1 || max_height == -1)
    return caps;

  gst_caps_set_simple (caps, "width", GST_TYPE_INT_RANGE, 1, max_width,
      "height", GST_TYPE_INT_RANGE, 1, max_height, NULL);

  return caps;
}

gboolean
gst_va_caps_from_profiles (GstVaDisplay * display, GArray * profiles,
    VAEntrypoint entrypoint, GstCaps ** codedcaps_ptr, GstCaps ** rawcaps_ptr)
{
  GstCaps *codedcaps, *rawcaps;
  VAProfile profile;
  gboolean ret;
  gint i, j, k;
  guint32 rt_formats;
  gint min_width = 1, max_width = G_MAXINT;
  gint min_height = 1, max_height = G_MAXINT;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), FALSE);
  g_return_val_if_fail (profiles, FALSE);

  codedcaps = gst_caps_new_empty ();
  rawcaps = gst_caps_new_empty ();

  for (i = 0; i < profiles->len; i++) {
    GstCaps *profile_codedcaps;

    profile = g_array_index (profiles, VAProfile, i);
    profile_codedcaps = gst_va_create_coded_caps (display, profile, entrypoint,
        &rt_formats);
    if (!profile_codedcaps)
      continue;

    for (j = 0; rt_formats && j < G_N_ELEMENTS (va_rt_format_list); j++) {
      if (rt_formats & va_rt_format_list[j]) {
        GstCaps *profile_rawcaps = gst_va_create_raw_caps (display, profile,
            entrypoint, va_rt_format_list[j]);

        if (!profile_rawcaps)
          continue;

        /* fetch width and height ranges */
        {
          guint num_structures = gst_caps_get_size (profile_rawcaps);

          for (k = 0; k < num_structures; k++) {
            GstStructure *st = gst_caps_get_structure (profile_rawcaps, k);
            if (!st)
              continue;
            if (gst_structure_has_field (st, "width")
                && gst_structure_has_field (st, "height")) {
              const GValue *w = gst_structure_get_value (st, "width");
              const GValue *h = gst_structure_get_value (st, "height");

              min_width = MAX (min_width, gst_value_get_int_range_min (w));
              max_width = MIN (max_width, gst_value_get_int_range_max (w));
              min_height = MAX (min_height, gst_value_get_int_range_min (h));
              max_height = MIN (max_height, gst_value_get_int_range_max (h));
            }
          }
        }

        rawcaps = gst_caps_merge (rawcaps, profile_rawcaps);
      }
    }

    /* check frame size range was specified otherwise use the one used
     * by the rawcaps */
    {
      guint num_structures = gst_caps_get_size (profile_codedcaps);

      for (k = 0; k < num_structures; k++) {
        GstStructure *st = gst_caps_get_structure (profile_codedcaps, k);
        if (!st)
          continue;
        if (!gst_structure_has_field (st, "width"))
          gst_structure_set (st, "width", GST_TYPE_INT_RANGE, min_width,
              max_width, NULL);
        if (!gst_structure_has_field (st, "height"))
          gst_structure_set (st, "height", GST_TYPE_INT_RANGE, min_height,
              max_height, NULL);
      }
    }

    codedcaps = gst_caps_merge (codedcaps, profile_codedcaps);
  }

  if (gst_caps_is_empty (rawcaps))
    gst_caps_replace (&rawcaps, NULL);
  if (gst_caps_is_empty (codedcaps))
    gst_caps_replace (&codedcaps, NULL);

  if ((ret = codedcaps && rawcaps)) {
    rawcaps = gst_caps_simplify (rawcaps);
    codedcaps = gst_caps_simplify (codedcaps);

    if (rawcaps_ptr)
      *rawcaps_ptr = gst_caps_ref (rawcaps);
    if (codedcaps_ptr)
      *codedcaps_ptr = gst_caps_ref (codedcaps);
  }

  if (codedcaps)
    gst_caps_unref (codedcaps);
  if (rawcaps)
    gst_caps_unref (rawcaps);

  return ret;
}
