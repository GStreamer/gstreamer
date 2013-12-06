/*
 *  gstvaapiutils_h264.h - H.264 related utilities
 *
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

#ifndef GST_VAAPI_UTILS_H264_H
#define GST_VAAPI_UTILS_H264_H

#include <va/va.h>
#include <gst/vaapi/gstvaapiprofile.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

/* Returns GstVaapiProfile from H.264 profile_idc value */
G_GNUC_INTERNAL
GstVaapiProfile
gst_vaapi_utils_h264_get_profile (guint8 profile_idc);

/* Returns H.264 profile_idc value from GstVaapiProfile */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_h264_get_profile_idc (GstVaapiProfile profile);

/* Returns GstVaapiChromaType from H.264 chroma_format_idc value */
G_GNUC_INTERNAL
GstVaapiChromaType
gst_vaapi_utils_h264_get_chroma_type (guint chroma_format_idc);

/* Returns H.264 chroma_format_idc value from GstVaapiChromaType */
G_GNUC_INTERNAL
guint
gst_vaapi_utils_h264_get_chroma_format_idc (GstVaapiChromaType chroma_type);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_H264_H */
