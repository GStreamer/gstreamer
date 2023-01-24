/*
 *  gstvaapiutils_core.c - VA-API utilities (Core, MT-safe)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
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
#include "gstvaapicompat.h"
#include "gstvaapiimage.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_core.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/**
 * gst_vaapi_get_config_attribute:
 * @display: a #GstVaapiDisplay
 * @profile: a VA profile
 * @entrypoint: a VA entrypoint
 * @type: a VA config attribute type
 * @out_value_ptr: return location for the config attribute value
 *
 * Determines the value for the VA config attribute @type and the
 * given @profile/@entrypoint pair. If @out_value_ptr is %NULL, then
 * this functions acts as a way to query whether the underlying VA
 * driver supports the specified attribute @type, no matter the
 * returned value.
 *
 * Note: this function only returns success if the VA driver does
 * actually know about this config attribute type and that it returned
 * a valid value for it.
 *
 * Return value: %TRUE if the VA driver knows about the requested
 *   config attribute and returned a valid value, %FALSE otherwise
 */
gboolean
gst_vaapi_get_config_attribute (GstVaapiDisplay * display, VAProfile profile,
    VAEntrypoint entrypoint, VAConfigAttribType type, guint * out_value_ptr)
{
  VAConfigAttrib attrib;
  VAStatus status;

  g_return_val_if_fail (display != NULL, FALSE);

  GST_VAAPI_DISPLAY_LOCK (display);
  attrib.type = type;
  status = vaGetConfigAttributes (GST_VAAPI_DISPLAY_VADISPLAY (display),
      profile, entrypoint, &attrib, 1);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaGetConfigAttributes()"))
    return FALSE;
  if (attrib.value == VA_ATTRIB_NOT_SUPPORTED)
    return FALSE;

  if (out_value_ptr)
    *out_value_ptr = attrib.value;
  return TRUE;
}

static VASurfaceAttrib *
get_surface_attributes (GstVaapiDisplay * display, VAConfigID config,
    guint * num_attribs)
{
  VASurfaceAttrib *surface_attribs = NULL;
  guint num_surface_attribs = 0;
  VAStatus va_status;

  if (config == VA_INVALID_ID)
    goto error;

  GST_VAAPI_DISPLAY_LOCK (display);
  va_status = vaQuerySurfaceAttributes (GST_VAAPI_DISPLAY_VADISPLAY (display),
      config, NULL, &num_surface_attribs);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (va_status, "vaQuerySurfaceAttributes()"))
    goto error;

  surface_attribs = g_malloc (num_surface_attribs * sizeof (*surface_attribs));
  if (!surface_attribs)
    goto error;

  GST_VAAPI_DISPLAY_LOCK (display);
  va_status = vaQuerySurfaceAttributes (GST_VAAPI_DISPLAY_VADISPLAY (display),
      config, surface_attribs, &num_surface_attribs);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (va_status, "vaQuerySurfaceAttributes()"))
    goto error;

  if (num_attribs)
    *num_attribs = num_surface_attribs;
  return surface_attribs;

  /* ERRORS */
error:
  {
    if (num_attribs)
      *num_attribs = -1;
    if (surface_attribs)
      g_free (surface_attribs);
    return NULL;
  }
}

/**
 * gst_vaapi_config_surface_attribures_get:
 * @display: a #GstVaapiDisplay
 * @config: a #VAConfigID
 *
 * Retrieves the possible surface attributes for the supplied config.
 *
 * Returns: (transfer full): returns a #GstVaapiConfigSurfaceAttributes
 **/
GstVaapiConfigSurfaceAttributes *
gst_vaapi_config_surface_attributes_get (GstVaapiDisplay * display,
    VAConfigID config)
{
  VASurfaceAttrib *surface_attribs;
  guint i, num_pixel_formats = 0, num_surface_attribs = 0;
  GstVaapiConfigSurfaceAttributes *attribs = NULL;

  surface_attribs =
      get_surface_attributes (display, config, &num_surface_attribs);
  if (!surface_attribs)
    return NULL;

  attribs = g_new0 (GstVaapiConfigSurfaceAttributes, 1);
  if (!attribs)
    goto error;

  for (i = 0; i < num_surface_attribs; i++) {
    const VASurfaceAttrib *const attrib = &surface_attribs[i];

    switch (attrib->type) {
      case VASurfaceAttribPixelFormat:
        if ((attrib->flags & VA_SURFACE_ATTRIB_SETTABLE)) {
          GstVideoFormat fmt;

          fmt = gst_vaapi_video_format_from_va_fourcc (attrib->value.value.i);
          if (fmt != GST_VIDEO_FORMAT_UNKNOWN)
            num_pixel_formats++;
        }
        break;
      case VASurfaceAttribMinWidth:
        attribs->min_width = attrib->value.value.i;
        break;
      case VASurfaceAttribMinHeight:
        attribs->min_height = attrib->value.value.i;
        break;
      case VASurfaceAttribMaxWidth:
        attribs->max_width = attrib->value.value.i;
        break;
      case VASurfaceAttribMaxHeight:
        attribs->max_height = attrib->value.value.i;
        break;
      case VASurfaceAttribMemoryType:
        attribs->mem_types = attrib->value.value.i;
        break;
      default:
        break;
    }
  }

  if (num_pixel_formats == 0) {
    attribs->formats = NULL;
  } else {
    attribs->formats = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoFormat),
        num_pixel_formats);

    for (i = 0; i < num_surface_attribs; i++) {
      const VASurfaceAttrib *const attrib = &surface_attribs[i];
      GstVideoFormat fmt;

      if (attrib->type != VASurfaceAttribPixelFormat)
        continue;
      if (!(attrib->flags & VA_SURFACE_ATTRIB_SETTABLE))
        continue;

      fmt = gst_vaapi_video_format_from_va_fourcc (attrib->value.value.i);
      if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
        continue;
      g_array_append_val (attribs->formats, fmt);
    }
  }

  g_free (surface_attribs);
  return attribs;

  /* ERRORS */
error:
  {
    g_free (surface_attribs);
    gst_vaapi_config_surface_attributes_free (attribs);
    return NULL;
  }
}

void
gst_vaapi_config_surface_attributes_free (GstVaapiConfigSurfaceAttributes *
    attribs)
{
  if (!attribs)
    return;

  if (attribs->formats)
    g_array_unref (attribs->formats);
  g_free (attribs);
}
