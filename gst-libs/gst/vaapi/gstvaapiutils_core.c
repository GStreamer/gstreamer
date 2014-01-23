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
