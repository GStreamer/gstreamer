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

static GstVaapiDisplay *
gst_vaapi_create_display_from_gl_context (GstObject * gl_context_object)
{
#if USE_GST_GL_HELPERS
  GstGLContext *const gl_context = GST_GL_CONTEXT (gl_context_object);
  GstGLDisplay *const gl_display = gst_gl_context_get_display (gl_context);
  gpointer native_display =
      GSIZE_TO_POINTER (gst_gl_display_get_handle (gl_display));
  GstVaapiDisplay *display, *out_display;
  GstVaapiDisplayType display_type;

  switch (gst_gl_display_get_handle_type (gl_display)) {
#if USE_X11
    case GST_GL_DISPLAY_TYPE_X11:
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
#if USE_X11
        if (!display_type && GST_GL_HAVE_WINDOW_X11)
          display_type = GST_VAAPI_DISPLAY_TYPE_X11;
#endif
#if USE_WAYLAND
        if (!display_type && GST_GL_HAVE_WINDOW_WAYLAND)
          display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
#endif
      }
      break;
    }
    default:
      display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
      break;
  }
  if (!display_type)
    return NULL;

  display = gst_vaapi_create_display_from_handle (display_type, native_display);
  if (!display)
    return NULL;

  switch (gst_gl_context_get_gl_platform (gl_context)) {
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
      if (!out_display)
        return NULL;
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

gboolean
gst_vaapi_ensure_display (GstElement * element, GstVaapiDisplayType type)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);
  GstVaapiDisplay *display;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  gst_vaapi_video_context_prepare (element);

  /* Neighbour found and it updated the display */
  if (gst_vaapi_plugin_base_has_display_type (plugin, type))
    return TRUE;

  /* If no neighboor, or application not interested, use system default */
  if (plugin->gl_context)
    display = gst_vaapi_create_display_from_gl_context (plugin->gl_context);
  else
    display = gst_vaapi_create_display (type, plugin->display_name);
  if (!display)
    return FALSE;

  gst_vaapi_video_context_propagate (element, display);
  GST_VAAPI_PLUGIN_BASE_DISPLAY_REPLACE (plugin, display);
  gst_vaapi_display_unref (display);
  return TRUE;
}

gboolean
gst_vaapi_reply_to_query (GstQuery * query, GstVaapiDisplay * display)
{
  const gchar *type = NULL;
  GstContext *context;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return FALSE;

  if (!display)
    return FALSE;

  if (!gst_query_parse_context_type (query, &type))
    return FALSE;

  if (g_strcmp0 (type, GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME))
    return FALSE;

  context = gst_vaapi_video_context_new_with_display (display, FALSE);
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

void
set_video_template_caps (GstCaps * caps)
{
  GstStructure *const structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
      "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
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
  GstCaps *caps;

  caps = gst_vaapi_video_format_new_template_caps (format);
  if (!caps)
    return NULL;

  GstCapsFeatures *const features =
      gst_caps_features_new (features_string, NULL);
  if (!features) {
    gst_caps_unref (caps);
    return NULL;
  }
  gst_caps_set_features (caps, 0, features);
  return caps;
}

static GstCaps *
new_gl_texture_upload_meta_caps (void)
{
  return
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
      (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
          "{ RGBA, BGRA }"));
}

GstVaapiCapsFeature
gst_vaapi_find_preferred_caps_feature (GstPad * pad, GstVideoFormat format,
    GstVideoFormat * out_format_ptr)
{
  GstVaapiCapsFeature feature = GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY;
  guint i, num_structures;
  GstCaps *caps = NULL;
  GstCaps *gl_texture_upload_caps = NULL;
  GstCaps *sysmem_caps = NULL;
  GstCaps *vaapi_caps = NULL;
  GstCaps *out_caps, *templ;
  GstVideoFormat out_format;

  templ = gst_pad_get_pad_template_caps (pad);
  out_caps = gst_pad_peer_query_caps (pad, templ);
  gst_caps_unref (templ);
  if (!out_caps) {
    feature = GST_VAAPI_CAPS_FEATURE_NOT_NEGOTIATED;
    goto cleanup;
  }

  out_format = format == GST_VIDEO_FORMAT_ENCODED ?
      GST_VIDEO_FORMAT_I420 : format;

  gl_texture_upload_caps = new_gl_texture_upload_meta_caps ();
  if (!gl_texture_upload_caps)
    goto cleanup;

  vaapi_caps =
      gst_vaapi_video_format_new_template_caps_with_features (out_format,
      GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE);
  if (!vaapi_caps)
    goto cleanup;

  sysmem_caps =
      gst_vaapi_video_format_new_template_caps_with_features (out_format,
      GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
  if (!sysmem_caps)
    goto cleanup;

  num_structures = gst_caps_get_size (out_caps);
  for (i = 0; i < num_structures; i++) {
    GstCapsFeatures *const features = gst_caps_get_features (out_caps, i);
    GstStructure *const structure = gst_caps_get_structure (out_caps, i);

#if GST_CHECK_VERSION(1,3,0)
    /* Skip ANY features, we need an exact match for correct evaluation */
    if (gst_caps_features_is_any (features))
      continue;
#endif

    caps = gst_caps_new_full (gst_structure_copy (structure), NULL);
    if (!caps)
      continue;
    gst_caps_set_features (caps, 0, gst_caps_features_copy (features));

    if (gst_caps_can_intersect (caps, vaapi_caps) &&
        feature < GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE)
      feature = GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE;
    else if (gst_caps_can_intersect (caps, gl_texture_upload_caps) &&
        feature < GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META)
      feature = GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META;
    else if (gst_caps_can_intersect (caps, sysmem_caps) &&
        feature < GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY)
      feature = GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY;
    gst_caps_replace (&caps, NULL);

#if GST_CHECK_VERSION(1,3,0)
    /* Stop at the first match, the caps should already be sorted out
       by preference order from downstream elements */
    if (feature != GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY)
      break;
#endif
  }

  if (out_format_ptr) {
    if (feature == GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META) {
      GstStructure *structure;
      gchar *format_str;
      out_format = GST_VIDEO_FORMAT_UNKNOWN;
      do {
        caps = gst_caps_intersect_full (out_caps, gl_texture_upload_caps,
            GST_CAPS_INTERSECT_FIRST);
        if (!caps)
          break;
        structure = gst_caps_get_structure (caps, 0);
        if (!structure)
          break;
        if (!gst_structure_get (structure, "format", G_TYPE_STRING,
                &format_str, NULL))
          break;
        out_format = gst_video_format_from_string (format_str);
        g_free (format_str);
      } while (0);
      if (!out_format)
        goto cleanup;
    }
    *out_format_ptr = out_format;
  }

cleanup:
  gst_caps_replace (&gl_texture_upload_caps, NULL);
  gst_caps_replace (&sysmem_caps, NULL);
  gst_caps_replace (&vaapi_caps, NULL);
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

/* Checks whether the supplied caps contain VA surfaces */
gboolean
gst_caps_has_vaapi_surface (GstCaps * caps)
{
  gboolean found_caps = FALSE;
  guint i, num_structures;

  g_return_val_if_fail (caps != NULL, FALSE);

  num_structures = gst_caps_get_size (caps);
  if (num_structures < 1)
    return FALSE;

  for (i = 0; i < num_structures && !found_caps; i++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, i);

#if GST_CHECK_VERSION(1,3,0)
    /* Skip ANY features, we need an exact match for correct evaluation */
    if (gst_caps_features_is_any (features))
      continue;
#endif

    found_caps = gst_caps_features_contains (features,
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE);
  }
  return found_caps;
}

void
gst_video_info_change_format (GstVideoInfo * vip, GstVideoFormat format,
    guint width, guint height)
{
  GstVideoInfo vi = *vip;

  gst_video_info_set_format (vip, format, width, height);

  vip->interlace_mode = vi.interlace_mode;
  vip->flags = vi.flags;
  vip->views = vi.views;
  vip->par_n = vi.par_n;
  vip->par_d = vi.par_d;
  vip->fps_n = vi.fps_n;
  vip->fps_d = vi.fps_d;
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
gst_vaapi_create_test_display ()
{
  return gst_vaapi_create_display (GST_VAAPI_DISPLAY_TYPE_ANY, NULL);
}
