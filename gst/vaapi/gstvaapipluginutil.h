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
#include "gstvaapivideomemory.h"

G_GNUC_INTERNAL
gboolean
gst_vaapi_ensure_display (GstElement * element, GstVaapiDisplayType type);

G_GNUC_INTERNAL
gboolean
gst_vaapi_handle_context_query (GstElement * element, GstQuery * query);

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
  GST_VAAPI_CAPS_FEATURE_NOT_NEGOTIATED,
  GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY,
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
gst_vaapi_find_preferred_caps_feature (GstPad * pad, GstCaps * allowed_caps,
    GstVideoFormat * out_format_ptr);

G_GNUC_INTERNAL
const gchar *
gst_vaapi_caps_feature_to_string (GstVaapiCapsFeature feature);

G_GNUC_INTERNAL
gboolean
gst_vaapi_caps_feature_contains (const GstCaps * caps,
    GstVaapiCapsFeature feature);

/* Helpers to handle interlaced contents */
# define GST_CAPS_INTERLACED_MODES \
    "interlace-mode = (string){ progressive, interleaved, mixed }"
# define GST_CAPS_INTERLACED_FALSE \
    "interlace-mode = (string)progressive"

#define GST_VAAPI_MAKE_SURFACE_CAPS					\
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(					\
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, "{ ENCODED, NV12, I420, YV12, P010_10LE }")

#define GST_VAAPI_MAKE_GLTEXUPLOAD_CAPS				\
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(					\
        GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, "{ RGBA, BGRA }")

G_GNUC_INTERNAL
gboolean
gst_caps_set_interlaced (GstCaps * caps, GstVideoInfo * vip);

G_GNUC_INTERNAL
gboolean
gst_caps_has_vaapi_surface (GstCaps * caps);

G_GNUC_INTERNAL
gboolean
gst_caps_is_video_raw (GstCaps * caps);

G_GNUC_INTERNAL
void
gst_video_info_change_format (GstVideoInfo * vip, GstVideoFormat format,
    guint width, guint height);

G_GNUC_INTERNAL
gboolean
gst_video_info_changed (const GstVideoInfo * old, const GstVideoInfo * new);

G_GNUC_INTERNAL
void
gst_video_info_force_nv12_if_encoded (GstVideoInfo * vinfo);

G_GNUC_INTERNAL
GstVaapiDisplay *
gst_vaapi_create_test_display (void);

G_GNUC_INTERNAL
gboolean
gst_vaapi_driver_is_whitelisted (GstVaapiDisplay * display);

G_GNUC_INTERNAL
gboolean
gst_vaapi_codecs_has_codec (GArray * codecs, GstVaapiCodec codec);

#endif /* GST_VAAPI_PLUGIN_UTIL_H */
