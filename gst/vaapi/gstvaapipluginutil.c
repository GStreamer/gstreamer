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

#include "gst/vaapi/sysdeps.h"
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
#if USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
#endif
#include "gstvaapipluginutil.h"
#include "gstvaapipluginbase.h"

/* Preferred first */
static const char *display_types[] = {
  "gst-vaapi-display",
  "vaapi-display",
#if USE_WAYLAND
  "wl-display",
  "wl-display-name",
#endif
#if USE_X11
  "x11-display",
  "x11-display-name",
#endif
#if USE_DRM
  "drm-device",
  "drm-device-path",
#endif
  NULL
};

typedef struct
{
  const gchar *type_str;
  GstVaapiDisplayType type;
  GstVaapiDisplay *(*create_display) (const gchar *);
} DisplayMap;

static const DisplayMap g_display_map[] = {
#if USE_WAYLAND
  {"wayland",
        GST_VAAPI_DISPLAY_TYPE_WAYLAND,
      gst_vaapi_display_wayland_new},
#endif
#if USE_GLX
  {"glx",
        GST_VAAPI_DISPLAY_TYPE_GLX,
      gst_vaapi_display_glx_new},
#endif
#if USE_X11
  {"x11",
        GST_VAAPI_DISPLAY_TYPE_X11,
      gst_vaapi_display_x11_new},
#endif
#if USE_DRM
  {"drm",
        GST_VAAPI_DISPLAY_TYPE_DRM,
      gst_vaapi_display_drm_new},
#endif
  {NULL,}
};

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

gboolean
gst_vaapi_ensure_display (gpointer element, GstVaapiDisplayType type)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);
  GstVaapiDisplay *display;
  GstVideoContext *context;

  g_return_val_if_fail (GST_IS_VIDEO_CONTEXT (element), FALSE);

  context = GST_VIDEO_CONTEXT (element);
  g_return_val_if_fail (context != NULL, FALSE);

  gst_vaapi_video_context_prepare (context, display_types);

  /* Neighbour found and it updated the display */
  if (gst_vaapi_plugin_base_has_display_type (plugin, type))
    return TRUE;

  /* If no neighboor, or application not interested, use system default */
  display = gst_vaapi_create_display (type, plugin->display_name);
  if (!display)
    return FALSE;

  gst_vaapi_video_context_propagate (context, display);
  GST_VAAPI_PLUGIN_BASE_DISPLAY_REPLACE (plugin, display);
  gst_vaapi_display_unref (display);
  return TRUE;
}

void
gst_vaapi_set_display (const gchar * type,
    const GValue * value, GstVaapiDisplay ** display_ptr)
{
  GstVaapiDisplay *display = NULL;

  if (!strcmp (type, "vaapi-display")) {
    g_return_if_fail (G_VALUE_HOLDS_POINTER (value));
    display = gst_vaapi_display_new_with_display (g_value_get_pointer (value));
  } else if (!strcmp (type, "gst-vaapi-display")) {
    g_return_if_fail (G_VALUE_HOLDS_POINTER (value));
    display = gst_vaapi_display_ref (g_value_get_pointer (value));
  }
#if USE_DRM
  else if (!strcmp (type, "drm-device")) {
    gint device;
    g_return_if_fail (G_VALUE_HOLDS_INT (value));
    device = g_value_get_int (value);
    display = gst_vaapi_display_drm_new_with_device (device);
  } else if (!strcmp (type, "drm-device-path")) {
    const gchar *device_path;
    g_return_if_fail (G_VALUE_HOLDS_STRING (value));
    device_path = g_value_get_string (value);
    display = gst_vaapi_display_drm_new (device_path);
  }
#endif
#if USE_X11
  else if (!strcmp (type, "x11-display-name")) {
    g_return_if_fail (G_VALUE_HOLDS_STRING (value));
#if USE_GLX
    display = gst_vaapi_display_glx_new (g_value_get_string (value));
#endif
    if (!display)
      display = gst_vaapi_display_x11_new (g_value_get_string (value));
  } else if (!strcmp (type, "x11-display")) {
    g_return_if_fail (G_VALUE_HOLDS_POINTER (value));
#if USE_GLX
    display =
        gst_vaapi_display_glx_new_with_display (g_value_get_pointer (value));
#endif
    if (!display)
      display =
          gst_vaapi_display_x11_new_with_display (g_value_get_pointer (value));
  }
#endif
#if USE_WAYLAND
  else if (!strcmp (type, "wl-display")) {
    struct wl_display *wl_display;
    g_return_if_fail (G_VALUE_HOLDS_POINTER (value));
    wl_display = g_value_get_pointer (value);
    display = gst_vaapi_display_wayland_new_with_display (wl_display);
  } else if (!strcmp (type, "wl-display-name")) {
    const gchar *display_name;
    g_return_if_fail (G_VALUE_HOLDS_STRING (value));
    display_name = g_value_get_string (value);
    display = gst_vaapi_display_wayland_new (display_name);
  }
#endif

  if (display) {
    gst_vaapi_display_replace (display_ptr, display);
    gst_vaapi_display_unref (display);
  }
}

gboolean
gst_vaapi_reply_to_query (GstQuery * query, GstVaapiDisplay * display)
{
#if GST_CHECK_VERSION(1,1,0)
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
#else
  GstVaapiDisplayType display_type;
  const gchar **types;
  const gchar *type;
  gint i;
  gboolean res = FALSE;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CUSTOM)
    return FALSE;

  if (!display)
    return FALSE;

  types = gst_video_context_query_get_supported_types (query);

  if (!types)
    return FALSE;

  display_type = gst_vaapi_display_get_display_type (display);
  for (i = 0; types[i] && !res; i++) {
    type = types[i];

    res = TRUE;
    if (!strcmp (type, "gst-vaapi-display")) {
      gst_video_context_query_set_pointer (query, type, display);
    } else if (!strcmp (type, "vaapi-display")) {
      VADisplay vadpy = gst_vaapi_display_get_display (display);
      gst_video_context_query_set_pointer (query, type, vadpy);
    } else {
      switch (display_type) {
#if USE_DRM
        case GST_VAAPI_DISPLAY_TYPE_DRM:{
          GstVaapiDisplayDRM *const drm_dpy = GST_VAAPI_DISPLAY_DRM (display);
          if (!strcmp (type, "drm-device-path"))
            gst_video_context_query_set_string (query, type,
                gst_vaapi_display_drm_get_device_path (drm_dpy));
#if 0
          /* XXX: gst_video_context_query_set_int() does not exist yet */
          else if (!strcmp (type, "drm-device"))
            gst_video_context_query_set_int (query, type,
                gst_vaapi_display_drm_get_device (drm_dpy));
#endif
          else
            res = FALSE;
          break;
        }
#endif
#if USE_X11
        case GST_VAAPI_DISPLAY_TYPE_X11:{
          GstVaapiDisplayX11 *const xvadpy = GST_VAAPI_DISPLAY_X11 (display);
          Display *const x11dpy = gst_vaapi_display_x11_get_display (xvadpy);
          if (!strcmp (type, "x11-display"))
            gst_video_context_query_set_pointer (query, type, x11dpy);
          else if (!strcmp (type, "x11-display-name"))
            gst_video_context_query_set_string (query, type,
                DisplayString (x11dpy));
          else
            res = FALSE;
          break;
        }
#endif
#if USE_WAYLAND
        case GST_VAAPI_DISPLAY_TYPE_WAYLAND:{
          GstVaapiDisplayWayland *const wlvadpy =
              GST_VAAPI_DISPLAY_WAYLAND (display);
          struct wl_display *const wldpy =
              gst_vaapi_display_wayland_get_display (wlvadpy);
          if (!strcmp (type, "wl-display"))
            gst_video_context_query_set_pointer (query, type, wldpy);
          else
            res = FALSE;
          break;
        }
#endif
        default:
          res = FALSE;
          break;
      }
    }
  }
  return res;
#endif /* !GST_CHECK_VERSION(1,1,0) */
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
#if GST_CHECK_VERSION(1,0,0)
  GstVideoOverlayCompositionMeta *const cmeta =
      gst_buffer_get_video_overlay_composition_meta (buffer);
  GstVideoOverlayComposition *composition = NULL;

  if (cmeta)
    composition = cmeta->overlay;
#else
  GstVideoOverlayComposition *const composition =
      gst_video_buffer_get_overlay_composition (buffer);
#endif
  return gst_vaapi_surface_set_subpictures_from_composition (surface,
      composition, TRUE);
}

gboolean
gst_vaapi_value_set_format (GValue * value, GstVideoFormat format)
{
#if GST_CHECK_VERSION(1,0,0)
  const gchar *str;

  str = gst_video_format_to_string (format);
  if (!str)
    return FALSE;

  g_value_init (value, G_TYPE_STRING);
  g_value_set_string (value, str);
#else
  guint32 fourcc;

  fourcc = gst_video_format_to_fourcc (format);
  if (!fourcc)
    return FALSE;

  g_value_init (value, GST_TYPE_FOURCC);
  gst_value_set_fourcc (value, fourcc);
#endif
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
#if GST_CHECK_VERSION(1,0,0)
  GstCaps *caps;

  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, NULL);

  caps = gst_caps_new_empty_simple ("video/x-raw");
  if (!caps)
    return NULL;

  gst_caps_set_simple (caps,
      "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
  set_video_template_caps (caps);
  return caps;
#else
  return gst_video_format_new_template_caps (format);
#endif
}

GstCaps *
gst_vaapi_video_format_new_template_caps_from_list (GArray * formats)
{
#if GST_CHECK_VERSION(1,0,0)
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
#else
  GstCaps *caps, *tmp_caps;
  guint i;

  g_return_val_if_fail (formats != NULL, NULL);

  caps = gst_caps_new_empty ();
  if (!caps)
    return NULL;

  for (i = 0; i < formats->len; i++) {
    const GstVideoFormat format = g_array_index (formats, GstVideoFormat, i);
    tmp_caps = gst_vaapi_video_format_new_template_caps (format);
    if (tmp_caps)
      gst_caps_append (caps, tmp_caps);
  }
#endif
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

#if GST_CHECK_VERSION(1,1,0)
  GstCapsFeatures *const features =
      gst_caps_features_new (features_string, NULL);
  if (!features) {
    gst_caps_unref (caps);
    return NULL;
  }
  gst_caps_set_features (caps, 0, features);
#endif
  return caps;
}

GstVaapiCapsFeature
gst_vaapi_find_preferred_caps_feature (GstPad * pad, GstVideoFormat format)
{
  GstVaapiCapsFeature feature = GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY;
#if GST_CHECK_VERSION(1,1,0)
  guint i, num_structures;
  GstCaps *caps = NULL;
  GstCaps *gl_texture_upload_caps = NULL;
  GstCaps *sysmem_caps = NULL;
  GstCaps *vaapi_caps = NULL;
  GstCaps *out_caps;

  out_caps = gst_pad_peer_query_caps (pad, NULL);
  if (!out_caps)
    goto cleanup;

  gl_texture_upload_caps =
      gst_vaapi_video_format_new_template_caps_with_features
      (GST_VIDEO_FORMAT_RGBA,
      GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META);
  if (!gl_texture_upload_caps)
    goto cleanup;

  if (format == GST_VIDEO_FORMAT_ENCODED)
    format = GST_VIDEO_FORMAT_I420;

  vaapi_caps =
      gst_vaapi_video_format_new_template_caps_with_features (format,
      GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE);
  if (!vaapi_caps)
    goto cleanup;

  sysmem_caps =
      gst_vaapi_video_format_new_template_caps_with_features (format,
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

cleanup:
  gst_caps_replace (&gl_texture_upload_caps, NULL);
  gst_caps_replace (&sysmem_caps, NULL);
  gst_caps_replace (&vaapi_caps, NULL);
  gst_caps_replace (&caps, NULL);
  gst_caps_replace (&out_caps, NULL);
#endif
  return feature;
}

const gchar *
gst_vaapi_caps_feature_to_string (GstVaapiCapsFeature feature)
{
  const gchar *str;

  switch (feature) {
#if GST_CHECK_VERSION(1,1,0)
    case GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY:
      str = GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY;
      break;
    case GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META:
      str = GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META;
      break;
    case GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE:
      str = GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE;
      break;
#endif
    default:
      str = NULL;
      break;
  }
  return str;
}

gboolean
gst_caps_set_interlaced (GstCaps * caps, GstVideoInfo * vip)
{
#if GST_CHECK_VERSION(1,0,0)
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
#else
  gst_caps_set_simple (caps, "interlaced", G_TYPE_BOOLEAN,
      vip ? GST_VIDEO_INFO_IS_INTERLACED (vip) : FALSE, NULL);
#endif
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

#if GST_CHECK_VERSION(1,1,0)
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
#else
  for (i = 0; i < num_structures && !found_caps; i++) {
    GstStructure *const structure = gst_caps_get_structure (caps, i);
    GstCaps *test_caps;
    GstVideoInfo vi;

    test_caps = gst_caps_new_full (gst_structure_copy (structure), NULL);
    if (!test_caps)
      continue;

    found_caps = gst_video_info_from_caps (&vi, test_caps) &&
        GST_VIDEO_INFO_FORMAT (&vi) == GST_VIDEO_FORMAT_ENCODED;
    gst_caps_unref (test_caps);
  }
#endif
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
