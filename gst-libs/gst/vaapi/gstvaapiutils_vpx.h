/*
 *  gstvaapiutils_vpx.h - vpx related utilities
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

#ifndef GST_VAAPI_UTILS_VPX_H
#define GST_VAAPI_UTILS_VPX_H

#include <gst/vaapi/gstvaapiprofile.h>

G_BEGIN_DECLS

/** Returns GstVaapiProfile from a string representation */
GstVaapiProfile
gst_vaapi_utils_vp9_get_profile_from_string (const gchar * str);

/** Returns a string representation for the supplied VP9 profile */
const gchar *
gst_vaapi_utils_vp9_get_profile_string (GstVaapiProfile profile);

guint
gst_vaapi_utils_vp9_get_chroma_format_idc (guint chroma_type);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_VPX_H */
