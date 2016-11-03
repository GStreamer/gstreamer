/* 
 * GStreamer
 * Copyright (C) 2013 Matthew Waters <ystreet00@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gstglutils
 * @short_description: some miscellaneous utilities for OpenGL
 * @see_also: #GstGLContext
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <gst/gst.h>
#include <glib/gprintf.h>

#include "gl.h"
#include "gstglutils.h"

#if GST_GL_HAVE_WINDOW_X11
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
#include <gst/gl/wayland/gstgldisplay_wayland.h>
#endif

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

struct _compile_shader
{
  GstGLShader **shader;
  const gchar *vertex_src;
  const gchar *fragment_src;
};

static void
_compile_shader (GstGLContext * context, struct _compile_shader *data)
{
  GstGLShader *shader;
  GstGLSLStage *vert, *frag;
  GError *error = NULL;

  shader = gst_gl_shader_new (context);

  if (data->vertex_src) {
    vert = gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
        GST_GLSL_VERSION_NONE,
        GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY, data->vertex_src);
    if (!gst_glsl_stage_compile (vert, &error)) {
      GST_ERROR_OBJECT (vert, "%s", error->message);
      gst_object_unref (vert);
      gst_object_unref (shader);
      return;
    }
    if (!gst_gl_shader_attach (shader, vert)) {
      gst_object_unref (shader);
      return;
    }
  }

  if (data->fragment_src) {
    frag = gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
        GST_GLSL_VERSION_NONE,
        GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
        data->fragment_src);
    if (!gst_glsl_stage_compile (frag, &error)) {
      GST_ERROR_OBJECT (frag, "%s", error->message);
      gst_object_unref (frag);
      gst_object_unref (shader);
      return;
    }
    if (!gst_gl_shader_attach (shader, frag)) {
      gst_object_unref (shader);
      return;
    }
  }

  if (!gst_gl_shader_link (shader, &error)) {
    GST_ERROR_OBJECT (shader, "%s", error->message);
    g_error_free (error);
    error = NULL;
    gst_gl_context_clear_shader (context);
    gst_object_unref (shader);
    return;
  }

  *data->shader = shader;
}

/* Called by glfilter */
gboolean
gst_gl_context_gen_shader (GstGLContext * context, const gchar * vert_src,
    const gchar * frag_src, GstGLShader ** shader)
{
  struct _compile_shader data;

  g_return_val_if_fail (frag_src != NULL || vert_src != NULL, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);

  data.shader = shader;
  data.vertex_src = vert_src;
  data.fragment_src = frag_src;

  gst_gl_context_thread_add (context, (GstGLContextThreadFunc) _compile_shader,
      &data);

  return *shader != NULL;
}

/* Called by glfilter */
void
gst_gl_context_del_shader (GstGLContext * context, GstGLShader * shader)
{
  gst_object_unref (shader);
}

static gboolean
gst_gl_display_found (GstElement * element, GstGLDisplay * display)
{
  if (display) {
    GST_LOG_OBJECT (element, "already have a display (%p)", display);
    return TRUE;
  }

  return FALSE;
}

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

static void
_init_context_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
#endif
}

static gboolean
pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *pad = g_value_get_object (item);
  GstQuery *query = user_data;
  gboolean res;

  _init_context_debug ();

  res = gst_pad_peer_query (pad, query);

  if (res) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, pad, "pad peer query failed");
  return TRUE;
}

gboolean
gst_gl_run_query (GstElement * element, GstQuery * query,
    GstPadDirection direction)
{
  GstIterator *it;
  GstIteratorFoldFunction func = pad_query;
  GValue res = { 0 };

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask neighbor */
  if (direction == GST_PAD_SRC)
    it = gst_element_iterate_src_pads (element);
  else
    it = gst_element_iterate_sink_pads (element);

  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);

  return g_value_get_boolean (&res);
}

static void
_gst_context_query (GstElement * element, const gchar * display_type)
{
  GstQuery *query;
  GstContext *ctxt;

  _init_context_debug ();

  /*  2a) Query downstream with GST_QUERY_CONTEXT for the context and
   *      check if downstream already has a context of the specific type
   *  2b) Query upstream as above.
   */
  query = gst_query_new_context (display_type);
  if (gst_gl_run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in downstream query", ctxt);
    gst_element_set_context (element, ctxt);
  } else if (gst_gl_run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in upstream query", ctxt);
    gst_element_set_context (element, ctxt);
  } else {
    /* 3) Post a GST_MESSAGE_NEED_CONTEXT message on the bus with
     *    the required context type and afterwards check if a
     *    usable context was set now as in 1). The message could
     *    be handled by the parent bins of the element and the
     *    application.
     */
    GstMessage *msg;

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting need context message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        display_type);
    gst_element_post_message (element, msg);
  }

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the element
   * is required to update the display_ptr or call gst_gl_handle_set_context().
   */

  gst_query_unref (query);
}

static void
gst_gl_display_context_query (GstElement * element, GstGLDisplay ** display_ptr)
{
  _gst_context_query (element, GST_GL_DISPLAY_CONTEXT_TYPE);
  if (*display_ptr)
    return;

#if GST_GL_HAVE_WINDOW_X11
  _gst_context_query (element, "gst.x11.display.handle");
  if (*display_ptr)
    return;
#endif

#if GST_GL_HAVE_WINDOW_WAYLAND
  _gst_context_query (element, "GstWaylandDisplayHandleContextType");
  if (*display_ptr)
    return;
#endif
}

static void
gst_gl_context_query (GstElement * element)
{
  _gst_context_query (element, "gst.gl.app_context");
}

/*  4) Create a context by itself and post a GST_MESSAGE_HAVE_CONTEXT
 *     message.
 */
static void
gst_gl_display_context_propagate (GstElement * element, GstGLDisplay * display)
{
  GstContext *context;
  GstMessage *msg;

  if (!display) {
    GST_ERROR_OBJECT (element, "Could not get GL display connection");
    return;
  }

  _init_context_debug ();

  context = gst_context_new (GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
  gst_context_set_gl_display (context, display);

  gst_element_set_context (element, context);

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting have context (%p) message with display (%p)", context, display);
  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
  gst_element_post_message (GST_ELEMENT_CAST (element), msg);
}

gboolean
gst_gl_ensure_element_data (gpointer element, GstGLDisplay ** display_ptr,
    GstGLContext ** context_ptr)
{
  GstGLDisplay *display;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (display_ptr != NULL, FALSE);
  g_return_val_if_fail (context_ptr != NULL, FALSE);

  /*  1) Check if the element already has a context of the specific
   *     type.
   */
  display = *display_ptr;
  if (gst_gl_display_found (element, display))
    goto done;

  gst_gl_display_context_query (element, display_ptr);

  /* Neighbour found and it updated the display */
  if (gst_gl_display_found (element, *display_ptr))
    goto get_gl_context;

  /* If no neighboor, or application not interested, use system default */
  display = gst_gl_display_new ();

  *display_ptr = display;

  gst_gl_display_context_propagate (element, display);

get_gl_context:
  if (*context_ptr)
    goto done;

  gst_gl_context_query (element);

done:
  return *display_ptr != NULL;
}

gboolean
gst_gl_handle_set_context (GstElement * element, GstContext * context,
    GstGLDisplay ** display, GstGLContext ** other_context)
{
  GstGLDisplay *display_replacement = NULL;
  GstGLContext *context_replacement = NULL;
  const gchar *context_type;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (other_context != NULL, FALSE);

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);

  if (g_strcmp0 (context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) {
    if (!gst_context_get_gl_display (context, &display_replacement)) {
      GST_WARNING_OBJECT (element, "Failed to get display from context");
      return FALSE;
    }
  }
#if GST_GL_HAVE_WINDOW_X11
  else if (g_strcmp0 (context_type, "gst.x11.display.handle") == 0) {
    const GstStructure *s;
    Display *display;

    s = gst_context_get_structure (context);
    if (gst_structure_get (s, "display", G_TYPE_POINTER, &display, NULL))
      display_replacement =
          (GstGLDisplay *) gst_gl_display_x11_new_with_display (display);
  }
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
  else if (g_strcmp0 (context_type, "GstWaylandDisplayHandleContextType") == 0) {
    const GstStructure *s;
    struct wl_display *display;

    s = gst_context_get_structure (context);
    if (gst_structure_get (s, "display", G_TYPE_POINTER, &display, NULL))
      display_replacement =
          (GstGLDisplay *) gst_gl_display_wayland_new_with_display (display);
  }
#endif
  else if (g_strcmp0 (context_type, "gst.gl.app_context") == 0) {
    const GstStructure *s = gst_context_get_structure (context);
    GstGLDisplay *context_display;
    GstGLDisplay *element_display;

    if (gst_structure_get (s, "context", GST_GL_TYPE_CONTEXT,
            &context_replacement, NULL)) {
      context_display = gst_gl_context_get_display (context_replacement);
      element_display = display_replacement ? display_replacement : *display;
      if (element_display
          && (gst_gl_display_get_handle_type (element_display) &
              gst_gl_display_get_handle_type (context_display)) == 0) {
        GST_ELEMENT_WARNING (element, LIBRARY, SETTINGS, ("%s",
                "Cannot set a GL context with a different display type"), ("%s",
                "Cannot set a GL context with a different display type"));
        gst_object_unref (context_replacement);
        context_replacement = NULL;
      }
      gst_object_unref (context_display);
    }
  }

  if (display_replacement) {
    GstGLDisplay *old = *display;
    *display = display_replacement;

    if (old)
      gst_object_unref (old);
  }

  if (context_replacement) {
    GstGLContext *old = *other_context;
    *other_context = context_replacement;

    if (old)
      gst_object_unref (old);
  }

  return TRUE;
}

gboolean
gst_gl_handle_context_query (GstElement * element, GstQuery * query,
    GstGLDisplay ** display, GstGLContext ** other_context)
{
  gboolean res = FALSE;
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (other_context != NULL, FALSE);

  gst_query_parse_context_type (query, &context_type);

  if (g_strcmp0 (context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) {

    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);

    gst_context_set_gl_display (context, *display);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *display != NULL;
  }
#if GST_GL_HAVE_WINDOW_X11
  else if (g_strcmp0 (context_type, "gst.x11.display.handle") == 0) {
    GstStructure *s;
    Display *x11_display = NULL;

    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new ("gst.x11.display.handle", TRUE);

    if (*display
        && ((*display)->type & GST_GL_DISPLAY_TYPE_X11) ==
        GST_GL_DISPLAY_TYPE_X11)
      x11_display = (Display *) gst_gl_display_get_handle (*display);

    s = gst_context_writable_structure (context);
    gst_structure_set (s, "display", G_TYPE_POINTER, x11_display, NULL);

    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = x11_display != NULL;
  }
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
  else if (g_strcmp0 (context_type, "GstWaylandDisplayHandleContextType") == 0) {
    GstStructure *s;
    struct wl_display *wayland_display = NULL;

    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new ("GstWaylandDisplayHandleContextType", TRUE);

    if (*display
        && ((*display)->type & GST_GL_DISPLAY_TYPE_WAYLAND) ==
        GST_GL_DISPLAY_TYPE_WAYLAND)
      wayland_display =
          (struct wl_display *) gst_gl_display_get_handle (*display);

    s = gst_context_writable_structure (context);
    gst_structure_set (s, "display", G_TYPE_POINTER, wayland_display, NULL);

    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = wayland_display != NULL;
  }
#endif
  else if (g_strcmp0 (context_type, "gst.gl.app_context") == 0) {
    GstStructure *s;

    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new ("gst.gl.app_context", TRUE);

    s = gst_context_writable_structure (context);
    gst_structure_set (s, "context", GST_GL_TYPE_CONTEXT, *other_context, NULL);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *other_context != NULL;
  }

  return res;
}

gsize
gst_gl_get_plane_data_size (GstVideoInfo * info, GstVideoAlignment * align,
    guint plane)
{
  gint padded_height;
  gsize plane_size;

  padded_height = info->height;

  if (align)
    padded_height += align->padding_top + align->padding_bottom;

  padded_height =
      GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (info->finfo, plane, padded_height);

  plane_size = GST_VIDEO_INFO_PLANE_STRIDE (info, plane) * padded_height;

  return plane_size;
}

/* find the difference between the start of the plane and where the video
 * data starts in the plane */
gsize
gst_gl_get_plane_start (GstVideoInfo * info, GstVideoAlignment * valign,
    guint plane)
{
  gsize plane_start;
  gint i;

  /* find the start of the plane data including padding */
  plane_start = 0;
  for (i = 0; i < plane; i++) {
    plane_start += gst_gl_get_plane_data_size (info, valign, i);
  }

  /* offset between the plane data start and where the video frame starts */
  return (GST_VIDEO_INFO_PLANE_OFFSET (info, plane)) - plane_start;
}

GstCaps *
gst_gl_caps_replace_all_caps_features (const GstCaps * caps,
    const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++) {
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (feature_name));
  }

  return tmp;
}

/**
 * gst_gl_value_get_texture_target_mask:
 * @value: an initialized #GValue of type G_TYPE_STRING
 *
 * See gst_gl_value_set_texture_target_from_mask() for what entails a mask
 *
 * Returns: the mask of #GstGLTextureTarget's in @value
 */
GstGLTextureTarget
gst_gl_value_get_texture_target_mask (const GValue * targets)
{
  guint new_targets = 0;

  g_return_val_if_fail (targets != NULL, GST_GL_TEXTURE_TARGET_NONE);

  if (G_TYPE_CHECK_VALUE_TYPE (targets, G_TYPE_STRING)) {
    GstGLTextureTarget target;
    const gchar *str;

    str = g_value_get_string (targets);
    target = gst_gl_texture_target_from_string (str);

    if (target)
      new_targets |= 1 << target;
  } else if (G_TYPE_CHECK_VALUE_TYPE (targets, GST_TYPE_LIST)) {
    gint j, m;

    m = gst_value_list_get_size (targets);
    for (j = 0; j < m; j++) {
      const GValue *val = gst_value_list_get_value (targets, j);
      GstGLTextureTarget target;
      const gchar *str;

      str = g_value_get_string (val);
      target = gst_gl_texture_target_from_string (str);
      if (target)
        new_targets |= 1 << target;
    }
  }

  return new_targets;
}

/**
 * gst_gl_value_set_texture_target:
 * @value: an initialized #GValue of type G_TYPE_STRING
 * @target: a #GstGLTextureTarget's
 *
 * Returns: whether the @target could be set on @value
 */
gboolean
gst_gl_value_set_texture_target (GValue * value, GstGLTextureTarget target)
{
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (target != GST_GL_TEXTURE_TARGET_NONE, FALSE);

  if (target == GST_GL_TEXTURE_TARGET_2D) {
    g_value_set_static_string (value, GST_GL_TEXTURE_TARGET_2D_STR);
  } else if (target == GST_GL_TEXTURE_TARGET_RECTANGLE) {
    g_value_set_static_string (value, GST_GL_TEXTURE_TARGET_RECTANGLE_STR);
  } else if (target == GST_GL_TEXTURE_TARGET_EXTERNAL_OES) {
    g_value_set_static_string (value, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR);
  } else {
    return FALSE;
  }

  return TRUE;
}

static guint64
_gst_gl_log2_int64 (guint64 value)
{
  guint64 ret = 0;

  while (value >>= 1)
    ret++;

  return ret;
}

/**
 * gst_gl_value_set_texture_target_from_mask:
 * @value: an uninitialized #GValue
 * @target_mask: a bitwise mask of #GstGLTextureTarget's
 *
 * A mask is a bitwise OR of (1 << target) where target is a valid
 * #GstGLTextureTarget
 *
 * Returns: whether the @target_mask could be set on @value
 */
gboolean
gst_gl_value_set_texture_target_from_mask (GValue * value,
    GstGLTextureTarget target_mask)
{
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (target_mask != GST_GL_TEXTURE_TARGET_NONE, FALSE);

  if ((target_mask & (target_mask - 1)) == 0) {
    /* only one texture target set */
    g_value_init (value, G_TYPE_STRING);
    return gst_gl_value_set_texture_target (value,
        _gst_gl_log2_int64 (target_mask));
  } else {
    GValue item = G_VALUE_INIT;
    gboolean ret = FALSE;

    g_value_init (value, GST_TYPE_LIST);
    g_value_init (&item, G_TYPE_STRING);
    if (target_mask & (1 << GST_GL_TEXTURE_TARGET_2D)) {
      gst_gl_value_set_texture_target (&item, GST_GL_TEXTURE_TARGET_2D);
      gst_value_list_append_value (value, &item);
      ret = TRUE;
    }
    if (target_mask & (1 << GST_GL_TEXTURE_TARGET_RECTANGLE)) {
      gst_gl_value_set_texture_target (&item, GST_GL_TEXTURE_TARGET_RECTANGLE);
      gst_value_list_append_value (value, &item);
      ret = TRUE;
    }
    if (target_mask & (1 << GST_GL_TEXTURE_TARGET_EXTERNAL_OES)) {
      gst_gl_value_set_texture_target (&item,
          GST_GL_TEXTURE_TARGET_EXTERNAL_OES);
      gst_value_list_append_value (value, &item);
      ret = TRUE;
    }

    return ret;
  }
}

static const gfloat identity_matrix[] = {
  1.0f, 0.0f, 0.0, 0.0f,
  0.0f, 1.0f, 0.0, 0.0f,
  0.0f, 0.0f, 1.0, 0.0f,
  0.0f, 0.0f, 0.0, 1.0f,
};

static const gfloat from_ndc_matrix[] = {
  0.5f, 0.0f, 0.0, 0.5f,
  0.0f, 0.5f, 0.0, 0.5f,
  0.0f, 0.0f, 0.5, 0.5f,
  0.0f, 0.0f, 0.0, 1.0f,
};

static const gfloat to_ndc_matrix[] = {
  2.0f, 0.0f, 0.0, -1.0f,
  0.0f, 2.0f, 0.0, -1.0f,
  0.0f, 0.0f, 2.0, -1.0f,
  0.0f, 0.0f, 0.0, 1.0f,
};

void
gst_gl_multiply_matrix4 (const gfloat * a, const gfloat * b, gfloat * result)
{
  int i, j, k;
  gfloat tmp[16] = { 0.0f };

  if (!a || !b || !result)
    return;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      for (k = 0; k < 4; k++) {
        tmp[i + (j * 4)] += a[i + (k * 4)] * b[k + (j * 4)];
      }
    }
  }

  for (i = 0; i < 16; i++)
    result[i] = tmp[i];
}

void
gst_gl_get_affine_transformation_meta_as_ndc (GstVideoAffineTransformationMeta *
    meta, gfloat * matrix)
{
  if (!meta) {
    int i;

    for (i = 0; i < 16; i++) {
      matrix[i] = identity_matrix[i];
    }
  } else {
    gfloat tmp[16] = { 0.0f };

    gst_gl_multiply_matrix4 (from_ndc_matrix, meta->matrix, tmp);
    gst_gl_multiply_matrix4 (tmp, to_ndc_matrix, matrix);
  }
}
