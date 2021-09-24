/*
 *  gstvaapiutils_core.h - VA-API utilities (Core, MT-safe)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
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

#ifndef GST_VAAPI_UTILS_CORE_H
#define GST_VAAPI_UTILS_CORE_H

#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

typedef struct _GstVaapiConfigSurfaceAttributes GstVaapiConfigSurfaceAttributes;


/**
 * GstVaapiConfigSurfaceAttributes:
 * @min_width: Minimal width in pixels.
 * @min_height: Minimal height in pixels.
 * @max_width: Maximal width in pixels.
 * @max_height: Maximal height in pixels.
 * @mem_types: Surface memory type expressed in bit fields.
 * @formats: Array of avialable GstVideoFormats of a surface in a VAConfig.
 *
 * Represents the possible surface attributes for the supplied config.
 **/
struct _GstVaapiConfigSurfaceAttributes
{
  gint min_width;
  gint min_height;
  gint max_width;
  gint max_height;
  guint mem_types;
  GArray *formats;
};

/* Gets attribute value for the supplied profile/entrypoint pair (MT-safe) */
G_GNUC_INTERNAL
gboolean
gst_vaapi_get_config_attribute (GstVaapiDisplay * display, VAProfile profile,
    VAEntrypoint entrypoint, VAConfigAttribType type, guint * out_value_ptr);

G_GNUC_INTERNAL
GstVaapiConfigSurfaceAttributes *
gst_vaapi_config_surface_attributes_get (GstVaapiDisplay * display, VAConfigID config);

G_GNUC_INTERNAL
void
gst_vaapi_config_surface_attributes_free (GstVaapiConfigSurfaceAttributes * attribs);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_CORE_H */
