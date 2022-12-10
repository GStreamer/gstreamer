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
#include <gst/vaapi/gstvaapiprofilecaps.h>
#include <gst/vaapi/gstvaapiutils.h>
#if GST_VAAPI_USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
#endif
#if GST_VAAPI_USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
#endif
#if GST_VAAPI_USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
#endif
#if GST_VAAPI_USE_EGL
# include <gst/vaapi/gstvaapidisplay_egl.h>
#endif
#if GST_VAAPI_USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
#endif
#if USE_GST_GL_HELPERS
# include <gst/gl/gl.h>
#if GST_VAAPI_USE_EGL && GST_GL_HAVE_PLATFORM_EGL
# include <gst/gl/egl/gstgldisplay_egl.h>
#endif
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
#if GST_VAAPI_USE_WAYLAND
  {"wayland",
   GST_VAAPI_DISPLAY_TYPE_WAYLAND,
   gst_vaapi_display_wayland_new,
   (GstVaapiDisplayCreateFromHandleFunc)
   gst_vaapi_display_wayland_new_with_display},
#endif
#if GST_VAAPI_USE_GLX
  {"glx",
   GST_VAAPI_DISPLAY_TYPE_GLX,
   gst_vaapi_display_glx_new,
   (GstVaapiDisplayCreateFromHandleFunc)
   gst_vaapi_display_glx_new_with_display},
#endif
#if GST_VAAPI_USE_X11
  {"x11",
   GST_VAAPI_DISPLAY_TYPE_X11,
   gst_vaapi_display_x11_new,
   (GstVaapiDisplayCreateFromHandleFunc)
   gst_vaapi_display_x11_new_with_display},
#endif
#if GST_VAAPI_USE_DRM
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

static GstVaapiDisplayType
gst_vaapi_get_display_type_from_gl (GstGLDisplayType gl_display_type,
    GstGLPlatform gl_platform)
{
  switch (gl_display_type) {
#if GST_VAAPI_USE_X11
    case GST_GL_DISPLAY_TYPE_X11:{
#if GST_VAAPI_USE_GLX
      if (gl_platform == GST_GL_PLATFORM_GLX)
        return GST_VAAPI_DISPLAY_TYPE_GLX;
#endif
      return GST_VAAPI_DISPLAY_TYPE_X11;
    }
#endif
#if GST_VAAPI_USE_WAYLAND
    case GST_GL_DISPLAY_TYPE_WAYLAND:{
      return GST_VAAPI_DISPLAY_TYPE_WAYLAND;
    }
#endif
#if GST_VAAPI_USE_EGL
    case GST_GL_DISPLAY_TYPE_EGL:{
      return GST_VAAPI_DISPLAY_TYPE_EGL;
    }
#endif
#if GST_VAAPI_USE_DRM
    case GST_GL_DISPLAY_TYPE_GBM:{
      return GST_VAAPI_DISPLAY_TYPE_DRM;
    }
#endif
    default:
      /* unsupported display. Still DRM may work. */
      break;
  }

  return GST_VAAPI_DISPLAY_TYPE_ANY;
}

static GstVaapiDisplayType
gst_vaapi_get_display_type_from_gl_env (void)
{
  const gchar *const gl_window_type = g_getenv ("GST_GL_WINDOW");

  if (!gl_window_type) {
#if GST_VAAPI_USE_X11 && GST_GL_HAVE_WINDOW_X11
    return GST_VAAPI_DISPLAY_TYPE_X11;
#elif GST_VAAPI_USE_WAYLAND && GST_GL_HAVE_WINDOW_WAYLAND
    return GST_VAAPI_DISPLAY_TYPE_WAYLAND;
#elif GST_VAAPI_USE_EGL && GST_GL_HAVE_PLATFORM_EGL
    return GST_VAAPI_DISPLAY_TYPE_EGL;
#endif
  }
#if GST_VAAPI_USE_X11
  if (g_strcmp0 (gl_window_type, "x11") == 0)
    return GST_VAAPI_DISPLAY_TYPE_X11;
#endif
#if GST_VAAPI_USE_WAYLAND
  if (g_strcmp0 (gl_window_type, "wayland") == 0)
    return GST_VAAPI_DISPLAY_TYPE_WAYLAND;
#endif
#if GST_VAAPI_USE_EGL
  {
    const gchar *const gl_platform_type = g_getenv ("GST_GL_PLATFORM");
    if (g_strcmp0 (gl_platform_type, "egl") == 0)
      return GST_VAAPI_DISPLAY_TYPE_EGL;
  }
#endif

  return GST_VAAPI_DISPLAY_TYPE_ANY;
}

#if GST_VAAPI_USE_EGL
static gint
gst_vaapi_get_gles_version_from_gl_api (GstGLAPI gl_api)
{
  switch (gl_api) {
    case GST_GL_API_GLES1:
      return 1;
    case GST_GL_API_GLES2:
      return 2;
    case GST_GL_API_OPENGL:
    case GST_GL_API_OPENGL3:
      return 0;
    default:
      break;
  }
  return -1;
}

static guintptr
gst_vaapi_get_egl_handle_from_gl_display (GstGLDisplay * gl_display)
{
  guintptr egl_handle = 0;
  GstGLDisplayEGL *egl_display;

  egl_display = gst_gl_display_egl_from_gl_display (gl_display);
  if (egl_display) {
    egl_handle = gst_gl_display_get_handle (GST_GL_DISPLAY (egl_display));
    gst_object_unref (egl_display);
  }
  return egl_handle;
}
#endif /* GST_VAAPI_USE_EGL */

static GstVaapiDisplay *
gst_vaapi_create_display_from_egl (GstGLDisplay * gl_display,
    GstGLContext * gl_context, GstVaapiDisplayType display_type,
    gpointer native_display)
{
  GstVaapiDisplay *display = NULL;
#if GST_VAAPI_USE_EGL
  GstGLAPI gl_api;
  gint gles_version;
  guintptr egl_handler;

  gl_api = gst_gl_context_get_gl_api (gl_context);
  gles_version = gst_vaapi_get_gles_version_from_gl_api (gl_api);
  if (gles_version == -1)
    return NULL;

  egl_handler = gst_vaapi_get_egl_handle_from_gl_display (gl_display);
  if (egl_handler != 0) {
    gpointer native_display_egl = GSIZE_TO_POINTER (egl_handler);
    display = gst_vaapi_display_egl_new_with_native_display (native_display_egl,
        display_type, gles_version);
  }

  if (!display) {
    GstVaapiDisplay *wrapped_display;

    wrapped_display =
        gst_vaapi_create_display_from_handle (display_type, native_display);
    if (wrapped_display) {
      display = gst_vaapi_display_egl_new (wrapped_display, gles_version);
      gst_object_unref (wrapped_display);
    }
  }

  if (display) {
    gst_vaapi_display_egl_set_gl_context (GST_VAAPI_DISPLAY_EGL (display),
        GSIZE_TO_POINTER (gst_gl_context_get_gl_context (gl_context)));
  }
#endif
  return display;
}
#endif /* USE_GST_GL_HELPERS */

static GstVaapiDisplay *
gst_vaapi_create_display_from_gl_context (GstObject * gl_context_object)
{
#if USE_GST_GL_HELPERS
  GstGLContext *const gl_context = GST_GL_CONTEXT (gl_context_object);
  GstGLDisplay *const gl_display = gst_gl_context_get_display (gl_context);
  GstGLDisplayType gl_display_type;
  GstGLPlatform gl_platform;
  gpointer native_display;
  GstVaapiDisplay *display = NULL;
  GstVaapiDisplayType display_type;

  /* Get display type and the native hanler */
  gl_display_type = gst_gl_display_get_handle_type (gl_display);
  gl_platform = gst_gl_context_get_gl_platform (gl_context);
  display_type =
      gst_vaapi_get_display_type_from_gl (gl_display_type, gl_platform);

  native_display = GSIZE_TO_POINTER (gst_gl_display_get_handle (gl_display));

  if (display_type == GST_VAAPI_DISPLAY_TYPE_ANY) {
    /* derive type and native_display from the active window */
    GstGLWindow *const gl_window = gst_gl_context_get_window (gl_context);
    if (gl_window)
      native_display = GSIZE_TO_POINTER (gst_gl_window_get_display (gl_window));
    display_type = gst_vaapi_get_display_type_from_gl_env ();
  }

  if (gl_platform == GST_GL_PLATFORM_EGL) {
    display = gst_vaapi_create_display_from_egl (gl_display, gl_context,
        display_type, native_display);
  }

  /* Non-EGL and fallback */
  if (!display) {
    display =
        gst_vaapi_create_display_from_handle (display_type, native_display);
  }

  gst_object_unref (gl_display);

  return display;
#endif
  GST_ERROR ("No GstGL support");
  return NULL;
}

static void
gst_vaapi_find_gl_context (GstElement * element)
{
#if USE_GST_GL_HELPERS
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);

  /* if the element is vaapisink or any vaapi encoder it doesn't need
   * to know a GstGLContext in order to create an appropriate
   * GstVaapiDisplay. Let's them to choose their own
   * GstVaapiDisplay */
  if (GST_IS_VIDEO_SINK (element) || GST_IS_VIDEO_ENCODER (element))
    return;

  if (!gst_gl_ensure_element_data (plugin,
          (GstGLDisplay **) & plugin->gl_display,
          (GstGLContext **) & plugin->gl_other_context))
    goto no_valid_gl_display;

  gst_vaapi_find_gl_local_context (element, &plugin->gl_context);

  if (plugin->gl_context) {
    gst_vaapi_plugin_base_set_srcpad_can_dmabuf (plugin, plugin->gl_context);
  } else {
    GstObject *gl_context;

    gl_context = gst_vaapi_plugin_base_create_gl_context (plugin);
    if (gl_context) {
      gst_vaapi_plugin_base_set_gl_context (plugin, gl_context);
      gst_object_unref (gl_context);
    }
  }

  /* ERRORS */
no_valid_gl_display:
  {
    GST_INFO_OBJECT (plugin, "No valid GL display found");
    gst_object_replace (&plugin->gl_display, NULL);
    gst_object_replace (&plugin->gl_other_context, NULL);
    return;
  }
#endif
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
  gst_object_unref (display);
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
      composition);
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
  GstCaps *peer_caps, *out_caps = NULL, *caps = NULL;
  static const guint feature_list[] = { GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE,
    GST_VAAPI_CAPS_FEATURE_DMABUF,
    GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META,
    GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY,
  };

  /* query with no filter */
  peer_caps = gst_pad_peer_query_caps (pad, NULL);
  if (!peer_caps)
    goto cleanup;
  if (gst_caps_is_empty (peer_caps))
    goto cleanup;

  /* filter against our allowed caps */
  out_caps = gst_caps_intersect_full (allowed_caps, peer_caps,
      GST_CAPS_INTERSECT_FIRST);

  /* default feature */
  feature = GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY;

  /* if downstream requests caps ANY, system memory is preferred */
  if (gst_caps_is_any (peer_caps))
    goto find_format;

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
    gst_caps_set_features_simple (caps, gst_caps_features_copy (features));

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

find_format:
  if (!caps)
    caps = gst_caps_ref (out_caps);

  if (out_format_ptr) {
    GstVideoFormat out_format;
    GstStructure *structure = NULL;
    const GValue *format_list;

    num_structures = gst_caps_get_size (caps);
    for (i = 0; i < num_structures; i++) {
      GstCapsFeatures *const features = gst_caps_get_features (caps, i);
      if (gst_caps_features_contains (features,
              gst_vaapi_caps_feature_to_string (feature))) {
        structure = gst_caps_get_structure (caps, i);
        break;
      }
    }
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
  gst_caps_replace (&peer_caps, NULL);
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
    case GST_VAAPI_CAPS_FEATURE_DMABUF:
      str = GST_CAPS_FEATURE_MEMORY_DMABUF;
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
 * gst_object_unref () after use. Or %NULL if no VA display is
 * available.
 **/
GstVaapiDisplay *
gst_vaapi_create_test_display (void)
{
  guint i;
  GstVaapiDisplay *display = NULL;
  const GstVaapiDisplayType test_display_map[] = {
#if GST_VAAPI_USE_DRM
    GST_VAAPI_DISPLAY_TYPE_DRM,
#endif
#if GST_VAAPI_USE_X11
    GST_VAAPI_DISPLAY_TYPE_X11,
#endif
  };

  for (i = 0; i < G_N_ELEMENTS (test_display_map); i++) {
    display = gst_vaapi_create_display (test_display_map[i], NULL);
    if (display)
      break;
  }
  return display;
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
    "Intel iHD driver",
    "Mesa Gallium driver",
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

  GST_WARNING ("Unsupported VA driver: %s. Export environment variable "
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

/**
 * gst_vaapi_encoder_get_profiles_from_caps:
 * @caps: a #GstCaps to detect
 * @func: a #GstVaapiStrToProfileFunc
 *
 * This function will detect all profile strings in @caps and
 * return the according GstVaapiProfile in array.
 *
 * Return: A #GArray of @GstVaapiProfile if succeed, %NULL if fail.
 **/
GArray *
gst_vaapi_encoder_get_profiles_from_caps (GstCaps * caps,
    GstVaapiStrToProfileFunc func)
{
  guint i, j;
  GstVaapiProfile profile;
  GArray *profiles = NULL;

  if (!caps)
    return NULL;

  profiles = g_array_new (FALSE, FALSE, sizeof (GstVaapiProfile));
  if (!profiles)
    return NULL;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *const structure = gst_caps_get_structure (caps, i);
    const GValue *const value = gst_structure_get_value (structure, "profile");

    if (value && G_VALUE_HOLDS_STRING (value)) {
      const gchar *str = g_value_get_string (value);
      if (str) {
        profile = func (str);
        if (profile == GST_VAAPI_PROFILE_H264_BASELINE)
          profile = GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;
        if (profile != GST_VAAPI_PROFILE_UNKNOWN)
          g_array_append_val (profiles, profile);
      }
    } else if (value && GST_VALUE_HOLDS_LIST (value)) {
      const GValue *v;
      const gchar *str;
      for (j = 0; j < gst_value_list_get_size (value); j++) {
        v = gst_value_list_get_value (value, j);
        if (!v || !G_VALUE_HOLDS_STRING (v))
          continue;

        str = g_value_get_string (v);
        if (str) {
          profile = func (str);
          if (profile != GST_VAAPI_PROFILE_UNKNOWN)
            g_array_append_val (profiles, profile);
        }
      }
    }
  }

  if (profiles->len == 0) {
    g_array_unref (profiles);
    profiles = NULL;
  }

  return profiles;
}

void
gst_vaapi_caps_set_width_and_height_range (GstCaps * caps, gint min_width,
    gint min_height, gint max_width, gint max_height)
{
  guint size, i;
  GstStructure *structure;

  /* Set the width/height info to caps */
  size = gst_caps_get_size (caps);
  for (i = 0; i < size; i++) {
    structure = gst_caps_get_structure (caps, i);
    if (!structure)
      continue;
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, min_width,
        max_width, "height", GST_TYPE_INT_RANGE, min_height, max_height,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  }
}

/**
 * gst_vaapi_build_caps_from_formats:
 * @formats: an array of supported #GstVideoFormat
 * @min_width: the min supported width
 * @min_height: the min supported height
 * @max_width: the max supported width
 * @max_height: the max supported height
 * @mem_types: the supported VA mem types
 *
 * This function generates a #GstCaps based on the information such as
 * formats, width and height.
 *
 * Return: A #GstCaps.
 **/
GstCaps *
gst_vaapi_build_caps_from_formats (GArray * formats, gint min_width,
    gint min_height, gint max_width, gint max_height, guint mem_types)
{
  GstCaps *out_caps, *raw_caps, *va_caps, *dma_caps;

  dma_caps = NULL;

  raw_caps = gst_vaapi_video_format_new_template_caps_from_list (formats);
  if (!raw_caps)
    return NULL;
  gst_vaapi_caps_set_width_and_height_range (raw_caps, min_width, min_height,
      max_width, max_height);

  va_caps = gst_caps_copy (raw_caps);
  gst_caps_set_features_simple (va_caps,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE));

  if (gst_vaapi_mem_type_supports (mem_types,
          GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF) ||
      gst_vaapi_mem_type_supports (mem_types,
          GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF2)) {
    dma_caps = gst_caps_copy (raw_caps);
    gst_caps_set_features_simple (dma_caps,
        gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_DMABUF));
  }

  out_caps = va_caps;
  if (dma_caps)
    gst_caps_append (out_caps, dma_caps);
  gst_caps_append (out_caps, raw_caps);

  return out_caps;
}

/**
 * gst_vaapi_build_template_raw_caps_by_codec:
 * @display: a #GstVaapiDisplay
 * @usage: used for encode, decode or postproc
 * @codec: a #GstVaapiCodec specify the codec to detect
 * @extra_fmts: a #GArray of extra #GstVideoFormat
 *
 * Called by vaapi elements to detect the all possible video formats belong to
 * the specified codec and build the caps. Only YUV kinds of formats are detected
 * because so far almost all codecs use YUV kinds of formats as input/output.
 * extra_fmts can specified more formats to be included.
 *
 * Returns: a built #GstCaps if succeeds, or %NULL if error.
 **/
GstCaps *
gst_vaapi_build_template_raw_caps_by_codec (GstVaapiDisplay * display,
    GstVaapiContextUsage usage, GstVaapiCodec codec, GArray * extra_fmts)
{
  GArray *profiles = NULL;
  GArray *supported_fmts = NULL;
  GstCaps *out_caps = NULL;
  guint i, e;
  GstVaapiProfile profile;
  guint value;
  guint chroma;
  GstVaapiChromaType gst_chroma;
  GstVaapiEntrypoint entrypoint_start, entrypoint_end;

  if (usage == GST_VAAPI_CONTEXT_USAGE_ENCODE) {
    profiles = gst_vaapi_display_get_encode_profiles (display);
    entrypoint_start = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
    entrypoint_end = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP;
  } else if (usage == GST_VAAPI_CONTEXT_USAGE_DECODE) {
    profiles = gst_vaapi_display_get_decode_profiles (display);
    entrypoint_start = GST_VAAPI_ENTRYPOINT_VLD;
    entrypoint_end = GST_VAAPI_ENTRYPOINT_MOCO;
  }
  /* TODO: VPP */

  if (!profiles)
    goto out;

  chroma = 0;
  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    if (gst_vaapi_profile_get_codec (profile) != codec)
      continue;

    for (e = entrypoint_start; e <= entrypoint_end; e++) {
      if (!gst_vaapi_get_config_attribute (display,
              gst_vaapi_profile_get_va_profile (profile),
              gst_vaapi_entrypoint_get_va_entrypoint (e),
              VAConfigAttribRTFormat, &value))
        continue;

      chroma |= value;
    }
  }

  if (!chroma)
    goto out;

  for (gst_chroma = GST_VAAPI_CHROMA_TYPE_YUV420;
      gst_chroma <= GST_VAAPI_CHROMA_TYPE_YUV444_12BPP; gst_chroma++) {
    GArray *fmts;
    if (!(chroma & from_GstVaapiChromaType (gst_chroma)))
      continue;

    fmts = gst_vaapi_video_format_get_formats_by_chroma (gst_chroma);
    if (!fmts)
      continue;

    /* One format can not belong to different chroma, no need to merge */
    if (supported_fmts == NULL) {
      supported_fmts = fmts;
    } else {
      for (i = 0; i < fmts->len; i++)
        g_array_append_val (supported_fmts,
            g_array_index (fmts, GstVideoFormat, i));
      g_array_unref (fmts);
    }
  }

  if (!supported_fmts)
    goto out;

  if (extra_fmts) {
    for (i = 0; i < extra_fmts->len; i++)
      g_array_append_val (supported_fmts,
          g_array_index (extra_fmts, GstVideoFormat, i));
  }

  out_caps = gst_vaapi_build_caps_from_formats (supported_fmts, 1, 1,
      G_MAXINT, G_MAXINT,
      from_GstVaapiBufferMemoryType (GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF));

out:
  if (profiles)
    g_array_unref (profiles);
  if (supported_fmts)
    g_array_unref (supported_fmts);

  return out_caps;
}

/**
 * gst_vaapi_structure_set_profiles:
 * @st: a #GstStructure
 * @list: a %NULL-terminated array of strings
 *
 * The @list of profiles are set in @st
 **/
void
gst_vaapi_structure_set_profiles (GstStructure * st, gchar ** list)
{
  guint i;
  GValue vlist = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;

  g_value_init (&vlist, GST_TYPE_LIST);
  g_value_init (&value, G_TYPE_STRING);

  for (i = 0; list[i]; i++) {
    g_value_set_string (&value, list[i]);
    gst_value_list_append_value (&vlist, &value);
  }

  if (i == 1)
    gst_structure_set_value (st, "profile", &value);
  else if (i > 1)
    gst_structure_set_value (st, "profile", &vlist);

  g_value_unset (&value);
  g_value_unset (&vlist);
}

/**
 * gst_vaapi_build_template_coded_caps_by_codec:
 * @display: a #GstVaapiDisplay
 * @usage: used for encode or decode
 * @codec: a #GstVaapiCodec specify the codec to detect
 * @caps_str: a string of basic caps
 *
 * Called by vaapi elements to detect the all possible profiles belong to the
 * specified codec and build the caps based on the basic caps description.
 *
 * Returns: a built #GstCaps if succeeds, or %NULL if error.
 **/
GstCaps *
gst_vaapi_build_template_coded_caps_by_codec (GstVaapiDisplay * display,
    GstVaapiContextUsage usage, GstVaapiCodec codec, const char *caps_str,
    GstVaapiProfileToStrFunc func)
{
  GValue v_profiles = G_VALUE_INIT;
  GValue v_profile = G_VALUE_INIT;
  GstCaps *caps = NULL;
  guint i, num;
  GArray *profiles = NULL;
  GstVaapiProfile profile;
  const gchar *str;

  caps = gst_caps_from_string (caps_str);
  if (!caps)
    goto out;

  if (!func)
    goto out;

  /* If no profiles, just ignore the profile field. */
  if (usage == GST_VAAPI_CONTEXT_USAGE_ENCODE) {
    profiles = gst_vaapi_display_get_encode_profiles (display);
  } else if (usage == GST_VAAPI_CONTEXT_USAGE_DECODE) {
    profiles = gst_vaapi_display_get_decode_profiles (display);
  }
  if (!profiles || profiles->len == 0)
    goto out;

  num = 0;
  g_value_init (&v_profiles, GST_TYPE_LIST);
  g_value_init (&v_profile, G_TYPE_STRING);

  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    if (gst_vaapi_profile_get_codec (profile) != codec)
      continue;

    str = func (profile);
    if (!str)
      continue;

    g_value_set_string (&v_profile, str);
    num++;
    gst_value_list_append_value (&v_profiles, &v_profile);
  }

  if (num == 1) {
    gst_caps_set_value (caps, "profile", &v_profile);
  } else if (num > 1) {
    gst_caps_set_value (caps, "profile", &v_profiles);
  }

out:
  g_value_unset (&v_profile);
  g_value_unset (&v_profiles);
  if (profiles)
    g_array_unref (profiles);

  return caps;
}
