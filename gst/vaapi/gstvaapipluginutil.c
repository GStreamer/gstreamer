/*
 *  gstvaapipluginutil.h - VA-API plugin helpers
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

#include "gstcompat.h"
#include "gstvaapivideocontext.h"
#if USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
#endif
#if USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
#endif
#if USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
#endif
#if USE_EGL
# include <gst/vaapi/gstvaapidisplay_egl.h>
#endif
#if USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
#endif
#include "gstvaapipluginutil.h"
#include "gstvaapipluginbase.h"

/* Environment variable for disable driver white-list */
#define GST_VAAPI_ALL_DRIVERS_ENV "GST_VAAPI_ALL_DRIVERS"

typedef GstVaapiDisplay *(*GstVaapiDisplayCreateFunc) (const gchar *);
typedef GstVaapiDisplay *(*GstVaapiDisplayCreateFromHandleFunc) (gpointer);

typedef struct
{
  const gchar *type_str;
  GstVaapiDisplayType type;
  GstVaapiDisplayCreateFunc create_display;
  GstVaapiDisplayCreateFromHandleFunc create_display_from_handle;
} DisplayMap;

/* *INDENT-OFF* */
static const DisplayMap g_display_map[] = {
#if USE_WAYLAND
  {"wayland",
   GST_VAAPI_DISPLAY_TYPE_WAYLAND,
   gst_vaapi_display_wayland_new,
   (GstVaapiDisplayCreateFromHandleFunc)
   gst_vaapi_display_wayland_new_with_display},
#endif
#if USE_GLX
  {"glx",
   GST_VAAPI_DISPLAY_TYPE_GLX,
   gst_vaapi_display_glx_new,
   (GstVaapiDisplayCreateFromHandleFunc)
   gst_vaapi_display_glx_new_with_display},
#endif
#if USE_X11
  {"x11",
   GST_VAAPI_DISPLAY_TYPE_X11,
   gst_vaapi_display_x11_new,
   (GstVaapiDisplayCreateFromHandleFunc)
   gst_vaapi_display_x11_new_with_display},
#endif
#if USE_DRM
  {"drm",
   GST_VAAPI_DISPLAY_TYPE_DRM,
   gst_vaapi_display_drm_new},
#endif
  {NULL,}
};
/* *INDENT-ON* */

static GstVaapiDisplay *
gst_vaapi_create_display (GstVaapiDisplayType display_type,
    const gchar * display_name)
{
  GstVaapiDisplay *display = NULL;
  const DisplayMap *m;

  for (m = g_display_map; m->type_str != NULL; m++) {
    if (display_type != GST_VAAPI_DISPLAY_TYPE_ANY && display_type != m->type)
      continue;

    display = m->create_display (display_name);
    if (display || display_type != GST_VAAPI_DISPLAY_TYPE_ANY)
      break;
  }
  return display;
}

#if USE_GST_GL_HELPERS
static GstVaapiDisplay *
gst_vaapi_create_display_from_handle (GstVaapiDisplayType display_type,
    gpointer handle)
{
  GstVaapiDisplay *display;
  const DisplayMap *m;

  if (display_type == GST_VAAPI_DISPLAY_TYPE_ANY)
    return NULL;

  for (m = g_display_map; m->type_str != NULL; m++) {
    if (m->type == display_type) {
      display = m->create_display_from_handle ?
          m->create_display_from_handle (handle) : NULL;
      return display;
    }
  }
  return NULL;
}
#endif

static GstVaapiDisplay *
gst_vaapi_create_display_from_gl_context (GstObject * gl_context_object)
{
#if USE_GST_GL_HELPERS
  GstGLContext *const gl_context = GST_GL_CONTEXT (gl_context_object);
  GstGLDisplay *const gl_display = gst_gl_context_get_display (gl_context);
  gpointer native_display =
      GSIZE_TO_POINTER (gst_gl_display_get_handle (gl_display));
  GstGLPlatform platform = gst_gl_context_get_gl_platform (gl_context);
  GstVaapiDisplay *display, *out_display;
  GstVaapiDisplayType display_type;

  switch (gst_gl_display_get_handle_type (gl_display)) {
#if USE_X11
    case GST_GL_DISPLAY_TYPE_X11:
#if USE_GLX
      if (platform == GST_GL_PLATFORM_GLX) {
        display_type = GST_VAAPI_DISPLAY_TYPE_GLX;
        break;
      }
#endif
      display_type = GST_VAAPI_DISPLAY_TYPE_X11;
      break;
#endif
#if USE_WAYLAND
    case GST_GL_DISPLAY_TYPE_WAYLAND:
      display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
      break;
#endif
    case GST_GL_DISPLAY_TYPE_ANY:{
      /* Derive from the active window */
      GstGLWindow *const gl_window = gst_gl_context_get_window (gl_context);
      const gchar *const gl_window_type = g_getenv ("GST_GL_WINDOW");

      display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
      if (!gl_window)
        break;
      native_display = GSIZE_TO_POINTER (gst_gl_window_get_display (gl_window));

      if (gl_window_type) {
#if USE_X11
        if (!display_type && g_strcmp0 (gl_window_type, "x11") == 0)
          display_type = GST_VAAPI_DISPLAY_TYPE_X11;
#endif
#if USE_WAYLAND
        if (!display_type && g_strcmp0 (gl_window_type, "wayland") == 0)
          display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
#endif
      } else {
#if USE_X11 && GST_GL_HAVE_WINDOW_X11
        if (!display_type)
          display_type = GST_VAAPI_DISPLAY_TYPE_X11;
#endif
#if USE_WAYLAND && GST_GL_HAVE_WINDOW_WAYLAND
        if (!display_type)
          display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
#endif
      }
      gst_object_unref (gl_window);
      break;
    }
    default:
      display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
      break;
  }
  gst_object_unref (gl_display);

  display = gst_vaapi_create_display_from_handle (display_type, native_display);
  if (!display)
    return NULL;

  switch (platform) {
#if USE_EGL
    case GST_GL_PLATFORM_EGL:{
      guint gles_version;

      switch (gst_gl_context_get_gl_api (gl_context)) {
        case GST_GL_API_GLES1:
          gles_version = 1;
          goto create_egl_display;
        case GST_GL_API_GLES2:
          gles_version = 2;
          goto create_egl_display;
        case GST_GL_API_OPENGL:
        case GST_GL_API_OPENGL3:
          gles_version = 0;
        create_egl_display:
          out_display = gst_vaapi_display_egl_new (display, gles_version);
          break;
        default:
          out_display = NULL;
          break;
      }
      if (!out_display) {
        gst_vaapi_display_unref (display);
        return NULL;
      }
      gst_vaapi_display_egl_set_gl_context (GST_VAAPI_DISPLAY_EGL (out_display),
          GSIZE_TO_POINTER (gst_gl_context_get_gl_context (gl_context)));
      break;
    }
#endif
    default:
      out_display = gst_vaapi_display_ref (display);
      break;
  }
  gst_vaapi_display_unref (display);
  return out_display;
#endif
  GST_ERROR ("unsupported GStreamer version %s", GST_API_VERSION_S);
  return NULL;
}

static void
gst_vaapi_find_gl_context (GstElement * element)
{
  GstObject *gl_context;
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);

  /* if the element is vaapisink or any vaapi encoder it doesn't need
   * to know a GstGLContext in order to create an appropriate
   * GstVaapiDisplay. Let's them to choose their own
   * GstVaapiDisplay */
  if (GST_IS_VIDEO_SINK (element) || GST_IS_VIDEO_ENCODER (element))
    return;

  gl_context = NULL;
  if (!gst_vaapi_find_gl_local_context (element, &gl_context))
    gl_context = gst_vaapi_plugin_base_create_gl_context (plugin);

  if (gl_context) {
    gst_vaapi_plugin_base_set_gl_context (plugin, gl_context);
    gst_object_unref (gl_context);
  }
}

gboolean
gst_vaapi_ensure_display (GstElement * element, GstVaapiDisplayType type)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);
  GstVaapiDisplay *display = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  if (gst_vaapi_video_context_prepare (element, &plugin->display)) {
    /* Neighbour found and it updated the display */
    if (gst_vaapi_plugin_base_has_display_type (plugin, type))
      return TRUE;
  }

  /* Query for a local GstGL context. If it's found, it will be used
   * to create the VA display */
  if (!plugin->gl_context)
    gst_vaapi_find_gl_context (element);

  /* If no neighboor, or application not interested, use system default */
  if (plugin->gl_context) {
    display = gst_vaapi_create_display_from_gl_context (plugin->gl_context);
    /* Cannot instantiate VA display based on GL context. Reset the
     *  requested display type to ANY to try again */
    if (!display)
      gst_vaapi_plugin_base_set_display_type (plugin,
          GST_VAAPI_DISPLAY_TYPE_ANY);
  }
  if (!display)
    display = gst_vaapi_create_display (type, plugin->display_name);
  if (!display)
    return FALSE;

  gst_vaapi_video_context_propagate (element, display);
  gst_vaapi_display_unref (display);
  return TRUE;
}

gboolean
gst_vaapi_handle_context_query (GstElement * element, GstQuery * query)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);
  const gchar *type = NULL;
  GstContext *context, *old_context;

  g_return_val_if_fail (query != NULL, FALSE);

#if USE_GST_GL_HELPERS
  if (plugin->gl_display && plugin->gl_context && plugin->gl_other_context) {
    if (gst_gl_handle_context_query (element, query,
            (GstGLDisplay *) plugin->gl_display,
            (GstGLContext *) plugin->gl_context,
            (GstGLContext *) plugin->gl_other_context))
      return TRUE;
  }
#endif

  if (!plugin->display)
    return FALSE;

  if (!gst_query_parse_context_type (query, &type))
    return FALSE;

  if (g_strcmp0 (type, GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME))
    return FALSE;

  gst_query_parse_context (query, &old_context);
  if (old_context) {
    context = gst_context_copy (old_context);
    gst_vaapi_video_context_set_display (context, plugin->display);
  } else {
    context = gst_vaapi_video_context_new_with_display (plugin->display, FALSE);
  }

  gst_query_set_context (query, context);
  gst_context_unref (context);

  return TRUE;
}

gboolean
gst_vaapi_append_surface_caps (GstCaps * out_caps, GstCaps * in_caps)
{
  GstStructure *structure;
  const GValue *v_width, *v_height, *v_framerate, *v_par;
  guint i, n_structures;

  structure = gst_caps_get_structure (in_caps, 0);
  v_width = gst_structure_get_value (structure, "width");
  v_height = gst_structure_get_value (structure, "height");
  v_framerate = gst_structure_get_value (structure, "framerate");
  v_par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (!v_width || !v_height)
    return FALSE;

  n_structures = gst_caps_get_size (out_caps);
  for (i = 0; i < n_structures; i++) {
    structure = gst_caps_get_structure (out_caps, i);
    gst_structure_set_value (structure, "width", v_width);
    gst_structure_set_value (structure, "height", v_height);
    if (v_framerate)
      gst_structure_set_value (structure, "framerate", v_framerate);
    if (v_par)
      gst_structure_set_value (structure, "pixel-aspect-ratio", v_par);
  }
  return TRUE;
}

gboolean
gst_vaapi_apply_composition (GstVaapiSurface * surface, GstBuffer * buffer)
{
  GstVideoOverlayCompositionMeta *const cmeta =
      gst_buffer_get_video_overlay_composition_meta (buffer);
  GstVideoOverlayComposition *composition = NULL;

  if (cmeta)
    composition = cmeta->overlay;
  return gst_vaapi_surface_set_subpictures_from_composition (surface,
      composition, TRUE);
}

gboolean
gst_vaapi_value_set_format (GValue * value, GstVideoFormat format)
{
  const gchar *str;

  str = gst_video_format_to_string (format);
  if (!str)
    return FALSE;

  g_value_init (value, G_TYPE_STRING);
  g_value_set_string (value, str);
  return TRUE;
}

gboolean
gst_vaapi_value_set_format_list (GValue * value, GArray * formats)
{
  GValue v_format = G_VALUE_INIT;
  guint i;

  g_value_init (value, GST_TYPE_LIST);
  for (i = 0; i < formats->len; i++) {
    GstVideoFormat const format = g_array_index (formats, GstVideoFormat, i);

    if (!gst_vaapi_value_set_format (&v_format, format))
      continue;
    gst_value_list_append_value (value, &v_format);
    g_value_unset (&v_format);
  }
  return TRUE;
}

static void
set_video_template_caps (GstCaps * caps)
{
  GstStructure *const structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

GstCaps *
gst_vaapi_video_format_new_template_caps (GstVideoFormat format)
{
  GstCaps *caps;

  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, NULL);

  caps = gst_caps_new_empty_simple ("video/x-raw");
  if (!caps)
    return NULL;

  gst_caps_set_simple (caps,
      "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
  set_video_template_caps (caps);
  return caps;
}

GstCaps *
gst_vaapi_video_format_new_template_caps_from_list (GArray * formats)
{
  GValue v_formats = G_VALUE_INIT;
  GstCaps *caps;

  caps = gst_caps_new_empty_simple ("video/x-raw");
  if (!caps)
    return NULL;

  if (!gst_vaapi_value_set_format_list (&v_formats, formats)) {
    gst_caps_unref (caps);
    return NULL;
  }

  gst_caps_set_value (caps, "format", &v_formats);
  set_video_template_caps (caps);
  g_value_unset (&v_formats);
  return caps;
}

GstCaps *
gst_vaapi_video_format_new_template_caps_with_features (GstVideoFormat format,
    const gchar * features_string)
{
  GstCapsFeatures *features;
  GstCaps *caps;

  caps = gst_vaapi_video_format_new_template_caps (format);
  if (!caps)
    return NULL;

  features = gst_caps_features_new (features_string, NULL);
  if (!features) {
    gst_caps_unref (caps);
    return NULL;
  }
  gst_caps_set_features (caps, 0, features);
  return caps;
}

static GstVideoFormat
gst_vaapi_find_preferred_format (const GValue * format_list,
    GstVideoFormat native_format)
{
  const GValue *frmt;
  GstVideoFormat out_format;
  guint i;

  /* if one format, that is the one */
  if (G_VALUE_HOLDS_STRING (format_list))
    return gst_video_format_from_string (g_value_get_string (format_list));

  if (!GST_VALUE_HOLDS_LIST (format_list)) {
    GST_ERROR ("negotiated caps do not have a valid format");
    return GST_VIDEO_FORMAT_UNKNOWN;
  }

  if (native_format == GST_VIDEO_FORMAT_UNKNOWN
      || native_format == GST_VIDEO_FORMAT_ENCODED) {
    native_format = GST_VIDEO_FORMAT_NV12;      /* default VA format */
  }

  /* search our native format in the list */
  for (i = 0; i < gst_value_list_get_size (format_list); i++) {
    frmt = gst_value_list_get_value (format_list, i);
    out_format = gst_video_format_from_string (g_value_get_string (frmt));

    /* GStreamer do not handle encoded formats nicely. Try the next
     * one. */
    if (out_format == GST_VIDEO_FORMAT_ENCODED)
      continue;

    if (native_format == out_format)
      return out_format;
  }

  /* just pick the first valid format in the list */
  i = 0;
  do {
    frmt = gst_value_list_get_value (format_list, i++);
    out_format = gst_video_format_from_string (g_value_get_string (frmt));
  } while (out_format == GST_VIDEO_FORMAT_ENCODED);

  return out_format;
}

GstVaapiCapsFeature
gst_vaapi_find_preferred_caps_feature (GstPad * pad, GstCaps * allowed_caps,
    GstVideoFormat * out_format_ptr)
{
  GstVaapiCapsFeature feature = GST_VAAPI_CAPS_FEATURE_NOT_NEGOTIATED;
  guint i, j, num_structures;
  GstCaps *out_caps, *caps = NULL;
  static const guint feature_list[] = { GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE,
    GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META,
    GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY,
  };

  out_caps = gst_pad_peer_query_caps (pad, allowed_caps);
  if (!out_caps)
    goto cleanup;

  if (gst_caps_is_any (out_caps) || gst_caps_is_empty (out_caps))
    goto cleanup;

  feature = GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY;
  num_structures = gst_caps_get_size (out_caps);
  for (i = 0; i < num_structures; i++) {
    GstCapsFeatures *const features = gst_caps_get_features (out_caps, i);
    GstStructure *const structure = gst_caps_get_structure (out_caps, i);

    /* Skip ANY features, we need an exact match for correct evaluation */
    if (gst_caps_features_is_any (features))
      continue;

    gst_caps_replace (&caps, NULL);
    caps = gst_caps_new_full (gst_structure_copy (structure), NULL);
    if (!caps)
      continue;
    gst_caps_set_features (caps, 0, gst_caps_features_copy (features));

    for (j = 0; j < G_N_ELEMENTS (feature_list); j++) {
      if (gst_vaapi_caps_feature_contains (caps, feature_list[j])
          && feature < feature_list[j]) {
        feature = feature_list[j];
        break;
      }
    }

    /* Stop at the first match, the caps should already be sorted out
       by preference order from downstream elements */
    if (feature != GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY)
      break;
  }

  if (!caps)
    goto cleanup;

  if (out_format_ptr) {
    GstVideoFormat out_format;
    GstStructure *structure;
    const GValue *format_list;

    /* if the best feature is SystemMemory, we should use the first
     * caps in the peer caps set, which is the preferred by
     * downstream. */
    if (feature == GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY)
      gst_caps_replace (&caps, out_caps);

    /* use the first caps, which is the preferred by downstream. */
    structure = gst_caps_get_structure (caps, 0);
    if (!structure)
      goto cleanup;
    format_list = gst_structure_get_value (structure, "format");
    if (!format_list)
      goto cleanup;
    out_format = gst_vaapi_find_preferred_format (format_list, *out_format_ptr);
    if (out_format == GST_VIDEO_FORMAT_UNKNOWN)
      goto cleanup;

    *out_format_ptr = out_format;
  }

cleanup:
  gst_caps_replace (&caps, NULL);
  gst_caps_replace (&out_caps, NULL);
  return feature;
}

const gchar *
gst_vaapi_caps_feature_to_string (GstVaapiCapsFeature feature)
{
  const gchar *str;

  switch (feature) {
    case GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY:
      str = GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY;
      break;
    case GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META:
      str = GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META;
      break;
    case GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE:
      str = GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE;
      break;
    default:
      str = NULL;
      break;
  }
  return str;
}

gboolean
gst_caps_set_interlaced (GstCaps * caps, GstVideoInfo * vip)
{
  GstVideoInterlaceMode mode;
  const gchar *mode_str;

  mode = vip ? GST_VIDEO_INFO_INTERLACE_MODE (vip) :
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  switch (mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
      mode_str = "progressive";
      break;
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
      mode_str = "interleaved";
      break;
    case GST_VIDEO_INTERLACE_MODE_MIXED:
      mode_str = "mixed";
      break;
    default:
      GST_ERROR ("unsupported `interlace-mode' %d", mode);
      return FALSE;
  }

  gst_caps_set_simple (caps, "interlace-mode", G_TYPE_STRING, mode_str, NULL);
  return TRUE;
}

static gboolean
_gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, i);
    /* Skip ANY features, we need an exact match for correct evaluation */
    if (gst_caps_features_is_any (features))
      continue;
    if (gst_caps_features_contains (features, feature))
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_vaapi_caps_feature_contains (const GstCaps * caps,
    GstVaapiCapsFeature feature)
{
  const gchar *feature_str;

  g_return_val_if_fail (caps != NULL, FALSE);

  feature_str = gst_vaapi_caps_feature_to_string (feature);
  if (!feature_str)
    return FALSE;

  return _gst_caps_has_feature (caps, feature_str);
}

/* Checks whether the supplied caps contain VA surfaces */
gboolean
gst_caps_has_vaapi_surface (GstCaps * caps)
{
  g_return_val_if_fail (caps != NULL, FALSE);

  return _gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE);
}

gboolean
gst_caps_is_video_raw (GstCaps * caps)
{
  GstStructure *structure;

  g_return_val_if_fail (caps != NULL, FALSE);

  if (!gst_caps_is_fixed (caps))
    return FALSE;
  if (!_gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
    return FALSE;
  structure = gst_caps_get_structure (caps, 0);
  return gst_structure_has_name (structure, "video/x-raw");
}

void
gst_video_info_change_format (GstVideoInfo * vip, GstVideoFormat format,
    guint width, guint height)
{
  GstVideoInfo vi = *vip;

  gst_video_info_set_format (vip, format, width, height);

  GST_VIDEO_INFO_INTERLACE_MODE (vip) = GST_VIDEO_INFO_INTERLACE_MODE (&vi);
  GST_VIDEO_FORMAT_INFO_FLAGS (vip) = GST_VIDEO_FORMAT_INFO_FLAGS (&vi);
  GST_VIDEO_INFO_VIEWS (vip) = GST_VIDEO_INFO_VIEWS (&vi);
  GST_VIDEO_INFO_PAR_N (vip) = GST_VIDEO_INFO_PAR_N (&vi);
  GST_VIDEO_INFO_PAR_D (vip) = GST_VIDEO_INFO_PAR_D (&vi);
  GST_VIDEO_INFO_FPS_N (vip) = GST_VIDEO_INFO_FPS_N (&vi);
  GST_VIDEO_INFO_FPS_D (vip) = GST_VIDEO_INFO_FPS_D (&vi);
  GST_VIDEO_INFO_MULTIVIEW_MODE (vip) = GST_VIDEO_INFO_MULTIVIEW_MODE (&vi);
  GST_VIDEO_INFO_MULTIVIEW_FLAGS (vip) = GST_VIDEO_INFO_MULTIVIEW_FLAGS (&vi);
}

/**
 * gst_video_info_changed:
 * @old: old #GstVideoInfo
 * @new: new #GstVideoInfo
 *
 * Compares @old and @new
 *
 * Returns: %TRUE if @old has different format/width/height than
 * @new. Otherwise, %FALSE.
 **/
gboolean
gst_video_info_changed (const GstVideoInfo * old, const GstVideoInfo * new)
{
  if (GST_VIDEO_INFO_FORMAT (old) != GST_VIDEO_INFO_FORMAT (new))
    return TRUE;
  if (GST_VIDEO_INFO_WIDTH (old) != GST_VIDEO_INFO_WIDTH (new))
    return TRUE;
  if (GST_VIDEO_INFO_HEIGHT (old) != GST_VIDEO_INFO_HEIGHT (new))
    return TRUE;
  return FALSE;
}

/**
 * gst_video_info_force_nv12_if_encoded:
 * @vinfo: a #GstVideoInfo
 *
 * If the format of @vinfo is %GST_VIDEO_FORMAT_ENCODED it is changed
 * to %GST_VIDEO_FORMAT_NV12.
 **/
void
gst_video_info_force_nv12_if_encoded (GstVideoInfo * vinfo)
{
  if (GST_VIDEO_INFO_FORMAT (vinfo) != GST_VIDEO_FORMAT_ENCODED)
    return;
  gst_video_info_set_format (vinfo, GST_VIDEO_FORMAT_NV12,
      GST_VIDEO_INFO_WIDTH (vinfo), GST_VIDEO_INFO_HEIGHT (vinfo));
}

/**
 * gst_vaapi_create_test_display:
 *
 * Creates a temporal #GstVaapiDisplay instance, just for testing the
 * supported features.
 *
 * Returns: a new #GstVaapiDisplay instances. Free with
 * gst_vaapi_display_unref () after use.
 **/
GstVaapiDisplay *
gst_vaapi_create_test_display (void)
{
  return gst_vaapi_create_display (GST_VAAPI_DISPLAY_TYPE_ANY, NULL);
}

/**
 * gst_vaapi_driver_is_whitelisted:
 * @display: a #GstVaapiDisplay
 *
 * Looks the VA-API driver vendors in an internal white-list.
 *
 * Returns: %TRUE if driver is in the white-list, otherwise %FALSE
 **/
gboolean
gst_vaapi_driver_is_whitelisted (GstVaapiDisplay * display)
{
  const gchar *vendor;
  guint i;
  static const gchar *whitelist[] = {
    "Intel i965 driver",
    "mesa gallium vaapi",
    NULL
  };

  g_return_val_if_fail (display, FALSE);

  if (g_getenv (GST_VAAPI_ALL_DRIVERS_ENV))
    return TRUE;

  vendor = gst_vaapi_display_get_vendor_string (display);
  if (!vendor)
    goto no_vendor;

  for (i = 0; whitelist[i]; i++) {
    if (g_ascii_strncasecmp (vendor, whitelist[i], strlen (whitelist[i])) == 0)
      return TRUE;
  }

  GST_ERROR ("Unsupported VA driver: %s. Export environment variable "
      GST_VAAPI_ALL_DRIVERS_ENV " to bypass", vendor);
  return FALSE;

  /* ERRORS */
no_vendor:
  {
    GST_WARNING ("no VA-API driver vendor description");
    return FALSE;
  }
}

/**
 * gst_vaapi_codecs_has_codec:
 * @codecs: a #GArray of #GstVaapiCodec
 * @codec: a #GstVaapiCodec to find in @codec
 *
 * Search in the available @codecs for the specific @codec.
 *
 * Returns: %TRUE if @codec is in @codecs
 **/
gboolean
gst_vaapi_codecs_has_codec (GArray * codecs, GstVaapiCodec codec)
{
  guint i;
  GstVaapiCodec c;

  g_return_val_if_fail (codec, FALSE);

  for (i = 0; i < codecs->len; i++) {
    c = g_array_index (codecs, GstVaapiCodec, i);
    if (c == codec)
      return TRUE;
  }
  return FALSE;
}
