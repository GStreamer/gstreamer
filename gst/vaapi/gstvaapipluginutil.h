/*
 *  gstvaapipluginutil.h - VA-API plugins private helper
 *
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2011 Collabora
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

#ifndef GST_VAAPI_PLUGIN_UTIL_H
#define GST_VAAPI_PLUGIN_UTIL_H

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideomemory.h"
#endif

G_GNUC_INTERNAL
gboolean
gst_vaapi_ensure_display (gpointer element, GstVaapiDisplayType type);

G_GNUC_INTERNAL
void
gst_vaapi_set_display (const gchar * type,
    const GValue * value, GstVaapiDisplay ** display_ptr);

G_GNUC_INTERNAL
gboolean
gst_vaapi_reply_to_query (GstQuery * query, GstVaapiDisplay * display);

G_GNUC_INTERNAL
gboolean
gst_vaapi_append_surface_caps (GstCaps * out_caps, GstCaps * in_caps);

G_GNUC_INTERNAL
gboolean
gst_vaapi_apply_composition (GstVaapiSurface * surface, GstBuffer * buffer);

#ifndef G_PRIMITIVE_SWAP
#define G_PRIMITIVE_SWAP(type, a, b) do {       \
        const type t = a; a = b; b = t;         \
    } while (0)
#endif

/* Helpers for GValue construction for video formats */
G_GNUC_INTERNAL
gboolean
gst_vaapi_value_set_format (GValue * value, GstVideoFormat format);

G_GNUC_INTERNAL
gboolean
gst_vaapi_value_set_format_list (GValue * value, GArray * formats);

/* Helpers to build video caps */
typedef enum
{
  GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY = 1,
  GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META,
  GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE,
} GstVaapiCapsFeature;

G_GNUC_INTERNAL
GstCaps *
gst_vaapi_video_format_new_template_caps (GstVideoFormat format);

G_GNUC_INTERNAL
GstCaps *
gst_vaapi_video_format_new_template_caps_from_list (GArray * formats);

G_GNUC_INTERNAL
GstCaps *
gst_vaapi_video_format_new_template_caps_with_features (GstVideoFormat format,
    const gchar * features_string);

G_GNUC_INTERNAL
GstVaapiCapsFeature
gst_vaapi_find_preferred_caps_feature (GstPad * pad, GstVideoFormat format);

/* Helpers to handle interlaced contents */
#if GST_CHECK_VERSION(1,0,0)
# define GST_CAPS_INTERLACED_MODES \
    "interlace-mode = (string){ progressive, interleaved, mixed }"
# define GST_CAPS_INTERLACED_FALSE \
    "interlace-mode = (string)progressive"
#else
# define GST_CAPS_INTERLACED_MODES \
    "interlaced = (boolean){ true, false }"
# define GST_CAPS_INTERLACED_FALSE \
    "interlaced = (boolean)false"
#endif

G_GNUC_INTERNAL
gboolean
gst_caps_set_interlaced (GstCaps * caps, GstVideoInfo * vip);

G_GNUC_INTERNAL
gboolean
gst_caps_has_vaapi_surface (GstCaps * caps);

#endif /* GST_VAAPI_PLUGIN_UTIL_H */
