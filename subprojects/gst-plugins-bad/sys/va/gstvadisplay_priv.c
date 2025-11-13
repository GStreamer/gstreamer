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

#include "gstvadisplay_priv.h"

#include <gst/va/gstvavideoformat.h>

#include "gstvaprofile.h"

GArray *
gst_va_display_get_profiles (GstVaDisplay * self, guint32 codec,
    VAEntrypoint entrypoint)
{
  GArray *ret = NULL;
  VADisplay dpy;
  VAEntrypoint *entrypoints;
  VAProfile *profiles;
  VAStatus status;
  gint i, j, num_entrypoints = 0, num_profiles = 0;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), NULL);

  dpy = gst_va_display_get_va_dpy (self);

  num_profiles = vaMaxNumProfiles (dpy);
  num_entrypoints = vaMaxNumEntrypoints (dpy);

  profiles = g_new (VAProfile, num_profiles);
  entrypoints = g_new (VAEntrypoint, num_entrypoints);

  status = vaQueryConfigProfiles (dpy, profiles, &num_profiles);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaQueryConfigProfile: %s", vaErrorStr (status));
    goto bail;
  }

  for (i = 0; i < num_profiles; i++) {
    if (codec != gst_va_profile_codec (profiles[i]))
      continue;

    status = vaQueryConfigEntrypoints (dpy, profiles[i], entrypoints,
        &num_entrypoints);
    if (status != VA_STATUS_SUCCESS) {
      GST_ERROR ("vaQueryConfigEntrypoints: %s", vaErrorStr (status));
      goto bail;
    }

    for (j = 0; j < num_entrypoints; j++) {
      if (entrypoints[j] == entrypoint) {
        if (!ret)
          ret = g_array_new (FALSE, FALSE, sizeof (VAProfile));
        g_array_append_val (ret, profiles[i]);
        break;
      }
    }
  }

bail:
  g_free (entrypoints);
  g_free (profiles);
  return ret;
}

GArray *
gst_va_display_get_image_formats (GstVaDisplay * self)
{
  GArray *ret = NULL;
  GstVideoFormat format;
  VADisplay dpy;
  VAImageFormat *va_formats;
  VAStatus status;
  int i, max, num = 0;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), NULL);

  dpy = gst_va_display_get_va_dpy (self);

  max = vaMaxNumImageFormats (dpy);
  if (max == 0)
    return NULL;

  va_formats = g_new (VAImageFormat, max);

  status = vaQueryImageFormats (dpy, va_formats, &num);

  gst_va_video_format_fix_map (va_formats, num);

  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaQueryImageFormats: %s", vaErrorStr (status));
    goto bail;
  }

  ret = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoFormat), num);
  for (i = 0; i < num; i++) {
    format = gst_va_video_format_from_va_image_format (&va_formats[i]);
    if (format != GST_VIDEO_FORMAT_UNKNOWN)
      g_array_append_val (ret, format);
  }

  if (ret->len == 0) {
    g_array_unref (ret);
    ret = NULL;
  }

bail:
  g_free (va_formats);
  return ret;
}

gboolean
gst_va_display_has_vpp (GstVaDisplay * self)
{
  VADisplay dpy;
  VAEntrypoint *entrypoints;
  VAStatus status;
  int i, max, num;
  gboolean found = FALSE;
  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  dpy = gst_va_display_get_va_dpy (self);

  max = vaMaxNumEntrypoints (dpy);

  entrypoints = g_new (VAEntrypoint, max);

  status = vaQueryConfigEntrypoints (dpy, VAProfileNone, entrypoints, &num);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaQueryConfigEntrypoints: %s", vaErrorStr (status));
    goto bail;
  }

  for (i = 0; i < num; i++) {
    if (entrypoints[i] == VAEntrypointVideoProc) {
      found = TRUE;
      break;
    }
  }

bail:
  g_free (entrypoints);
  return found;
}

#define _get_config_attrib(type) \
    __get_config_attrib(self, profile, entrypoint, &attrib, type, G_STRINGIFY (type))

static inline int
__get_config_attrib (GstVaDisplay * self, VAProfile profile,
    VAEntrypoint entrypoint, VAConfigAttrib * attrib,
    VAConfigAttribType type, const char *name)
{
  VAStatus status;
  VADisplay dpy;

  g_return_val_if_fail (profile != VAProfileNone, 0);

  /* *INDENT-OFF* */
  *attrib = (VAConfigAttrib) {
    .type = type,
  };
  /* *INDENT-ON* */

  dpy = gst_va_display_get_va_dpy (self);
  status = vaGetConfigAttributes (dpy, profile, entrypoint, attrib, 1);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING_OBJECT (self, "vaGetConfigAttributes (%s): %s", name,
        vaErrorStr (status));
    return 0;
  }

  if (attrib->value == VA_ATTRIB_NOT_SUPPORTED) {
    GST_WARNING_OBJECT (self, "Driver does not support attribute %s", name);
    return -1;
  }

  return 1;
}

gint32
gst_va_display_get_max_slice_num (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), -1);

  if (_get_config_attrib (VAConfigAttribEncMaxSlices) < 1)
    return -1;

  return attrib.value;
}

guint32
gst_va_display_get_slice_structure (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), 0);

  if (_get_config_attrib (VAConfigAttribEncSliceStructure) < 1)
    return 0;

  return attrib.value;
}

gboolean
gst_va_display_get_max_num_reference (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint,
    guint32 * list0, guint32 * list1)
{
  VAConfigAttrib attrib;
  int ret;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  ret = _get_config_attrib (VAConfigAttribEncMaxRefFrames);

  if (ret == 0)
    return FALSE;

  if (ret == -1) {
    if (list0)
      *list0 = 0;
    if (list1)
      *list1 = 0;

    return TRUE;
  }

  if (list0)
    *list0 = attrib.value & 0xffff;
  if (list1)
    *list1 = (attrib.value >> 16) & 0xffff;

  return TRUE;
}

guint32
gst_va_display_get_prediction_direction (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), 0);

  if (_get_config_attrib (VAConfigAttribPredictionDirection) < 1)
    return 0;

  /* supported prediction directions */
  return attrib.value & (VA_PREDICTION_DIRECTION_PREVIOUS |
      VA_PREDICTION_DIRECTION_FUTURE | VA_PREDICTION_DIRECTION_BI_NOT_EMPTY);
}

guint32
gst_va_display_get_rate_control_mode (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib = {.type = VAConfigAttribRateControl };

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), 0);

  if (_get_config_attrib (VAConfigAttribRateControl) < 1)
    return 0;

  return attrib.value;
}

guint32
gst_va_display_get_quality_level (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), 0);

  if (_get_config_attrib (VAConfigAttribEncQualityRange) < 1)
    return 0;

  return attrib.value;
}

gboolean
gst_va_display_has_trellis (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  if (_get_config_attrib (VAConfigAttribEncQuantization) < 1)
    return FALSE;

  return (gboolean) (attrib.value & VA_ENC_QUANTIZATION_TRELLIS_SUPPORTED);
}

gboolean
gst_va_display_has_tile (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  if (_get_config_attrib (VAConfigAttribEncTileSupport) < 1)
    return FALSE;

  return (attrib.value > 0);
}

guint32
gst_va_display_get_rtformat (GstVaDisplay * self,
    VAProfile profile, VAEntrypoint entrypoint)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  if (_get_config_attrib (VAConfigAttribRTFormat) < 1)
    return 0;

  return attrib.value;
}

gboolean
gst_va_display_get_packed_headers (GstVaDisplay * self, VAProfile profile,
    VAEntrypoint entrypoint, guint32 * packed_headers)
{
  VAConfigAttrib attrib;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  if (_get_config_attrib (VAConfigAttribEncPackedHeaders) < 1)
    return FALSE;

  if (packed_headers)
    *packed_headers = attrib.value;
  return TRUE;
}
