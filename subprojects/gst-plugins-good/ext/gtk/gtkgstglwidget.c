/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gtkgstglwidget.h"
#include "gstgtkutils.h"
#include <gst/gl/gstglfuncs.h>
#include <gst/video/video.h>

#if GST_GL_HAVE_WINDOW_X11 && defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif

#if GST_GL_HAVE_WINDOW_WAYLAND && defined (GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#include <gst/gl/wayland/gstgldisplay_wayland.h>
#endif

#if GST_GL_HAVE_WINDOW_WIN32 && defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#endif

/**
 * SECTION:gtkgstglwidget
 * @title: GtkGstGlWidget
 * @short_description: a #GtkGLArea that renders GStreamer video #GstBuffers
 * @see_also: #GtkGLArea, #GstBuffer
 *
 * #GtkGstGLWidget is an #GtkWidget that renders GStreamer video buffers.
 */

#define GST_CAT_DEFAULT gtk_gst_gl_widget_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

struct _GtkGstGLWidgetPrivate
{
  gboolean initted;
  GstGLDisplay *display;
  GdkGLContext *gdk_context;
  GstGLContext *other_context;
  GstGLContext *context;
  GstGLUpload *upload;
  GstGLShader *shader;
  GLuint vao;
  GLuint vertex_buffer;
  GLint attr_position;
  GLint attr_texture;
  GLuint current_tex;
  GstVideoOrientationMethod rotate_method;
  GstVideoOrientationMethod current_rotate_method;
  const gfloat *transform_matrix;
  gboolean is_wgl;
};

static const GLfloat vertices[] = {
  1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
  1.0f, -1.0f, 0.0f, 1.0f, 1.0f
};

G_DEFINE_TYPE_WITH_CODE (GtkGstGLWidget, gtk_gst_gl_widget, GTK_TYPE_GL_AREA,
    G_ADD_PRIVATE (GtkGstGLWidget)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gtkgstglwidget", 0,
        "Gtk Gst GL Widget");
    );

static void
gtk_gst_gl_widget_bind_buffer (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  const GstGLFuncs *gl = priv->context->gl_vtable;

  gl->BindBuffer (GL_ARRAY_BUFFER, priv->vertex_buffer);

  /* Load the vertex position */
  gl->VertexAttribPointer (priv->attr_position, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (priv->attr_texture, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (priv->attr_position);
  gl->EnableVertexAttribArray (priv->attr_texture);
}

static void
gtk_gst_gl_widget_unbind_buffer (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  const GstGLFuncs *gl = priv->context->gl_vtable;

  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (priv->attr_position);
  gl->DisableVertexAttribArray (priv->attr_texture);
}

static void
gtk_gst_gl_widget_init_redisplay (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  const GstGLFuncs *gl = priv->context->gl_vtable;
  GError *error = NULL;
  GstGLSLStage *frag_stage, *vert_stage;

  vert_stage = gst_glsl_stage_new_with_string (priv->context,
      GL_VERTEX_SHADER, GST_GLSL_VERSION_NONE,
      GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
      gst_gl_shader_string_vertex_mat4_vertex_transform);
  frag_stage = gst_glsl_stage_new_default_fragment (priv->context);

  gst_gl_insert_debug_marker (priv->other_context, "initializing redisplay");
  if (!(priv->shader =
          gst_gl_shader_new_link_with_stages (priv->context, &error, vert_stage,
              frag_stage, NULL))) {
    GST_ERROR ("Failed to initialize shader: %s", error->message);
    return;
  }

  priv->attr_position =
      gst_gl_shader_get_attribute_location (priv->shader, "a_position");
  priv->attr_texture =
      gst_gl_shader_get_attribute_location (priv->shader, "a_texcoord");

  if (gl->GenVertexArrays) {
    gl->GenVertexArrays (1, &priv->vao);
    gl->BindVertexArray (priv->vao);
  }

  gl->GenBuffers (1, &priv->vertex_buffer);
  gl->BindBuffer (GL_ARRAY_BUFFER, priv->vertex_buffer);
  gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
      GL_STATIC_DRAW);

  if (gl->GenVertexArrays) {
    gtk_gst_gl_widget_bind_buffer (gst_widget);
    gl->BindVertexArray (0);
  }

  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  priv->initted = TRUE;
}

/* rotate 90 */
static const gfloat clockwise_matrix[] = {
  0.0f, -1.0f, 0.0f, 0.0f,
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

/* rotate 180 */
static const gfloat clockwise_180_matrix[] = {
  -1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, -1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

/* rotate 270 */
static const gfloat counterclockwise_matrix[] = {
  0.0f, 1.0f, 0.0f, 0.0f,
  -1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

/* horizontal-flip */
static const gfloat horizontal_flip_matrix[] = {
  -1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

/* vertical-flip */
static const gfloat vertical_flip_matrix[] = {
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, -1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

/* upper-left-diagonal */
static const gfloat upper_left_matrix[] = {
  0.0f, 1.0f, 0.0f, 0.0f,
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

/* upper-right-diagonal */
static const gfloat upper_right_matrix[] = {
  0.0f, -1.0f, 0.0f, 0.0f,
  -1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

static void
_redraw_texture (GtkGstGLWidget * gst_widget, guint tex)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  const GstGLFuncs *gl = priv->context->gl_vtable;
  const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (gst_widget);
  GtkWidget *widget = GTK_WIDGET (gst_widget);


  if (gst_widget->base.force_aspect_ratio) {
    GstVideoRectangle src, dst, result;
    gint video_width, video_height, widget_scale;

    gl->ClearColor (0.0, 0.0, 0.0, 0.0);
    gl->Clear (GL_COLOR_BUFFER_BIT);

    widget_scale = gtk_widget_get_scale_factor (widget);

    if (priv->current_rotate_method == GST_VIDEO_ORIENTATION_90R
        || priv->current_rotate_method == GST_VIDEO_ORIENTATION_90L
        || priv->current_rotate_method == GST_VIDEO_ORIENTATION_UL_LR
        || priv->current_rotate_method == GST_VIDEO_ORIENTATION_UR_LL) {
      video_width = base_widget->display_height;
      video_height = base_widget->display_width;
    } else {
      video_width = base_widget->display_width;
      video_height = base_widget->display_height;
    }

    src.x = 0;
    src.y = 0;
    src.w = video_width;
    src.h = video_height;

    dst.x = 0;
    dst.y = 0;
    dst.w = gtk_widget_get_allocated_width (widget) * widget_scale;
    dst.h = gtk_widget_get_allocated_height (widget) * widget_scale;

    gst_video_sink_center_rect (src, dst, &result, TRUE);

    GST_LOG ("Center src %dx%d into dst %dx%d result -> %dx%d",
        src.w, src.h, dst.w, dst.h, result.w, result.h);

    gl->Viewport (result.x, result.y, result.w, result.h);
  }

  gst_gl_shader_use (priv->shader);

  if (gl->BindVertexArray)
    gl->BindVertexArray (priv->vao);
  gtk_gst_gl_widget_bind_buffer (gst_widget);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, tex);
  gst_gl_shader_set_uniform_1i (priv->shader, "tex", 0);

  {
    GstVideoAffineTransformationMeta *af_meta;
    gfloat matrix[16];

    af_meta =
        gst_buffer_get_video_affine_transformation_meta (base_widget->buffer);

    if (priv->transform_matrix) {
      gfloat tmp[16];

      gst_gl_get_affine_transformation_meta_as_ndc (af_meta, tmp);
      gst_gl_multiply_matrix4 (tmp, priv->transform_matrix, matrix);
    } else {
      gst_gl_get_affine_transformation_meta_as_ndc (af_meta, matrix);
    }

    gst_gl_shader_set_uniform_matrix_4fv (priv->shader,
        "u_transformation", 1, FALSE, matrix);
  }

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  if (gl->BindVertexArray)
    gl->BindVertexArray (0);
  else
    gtk_gst_gl_widget_unbind_buffer (gst_widget);

  gl->BindTexture (GL_TEXTURE_2D, 0);
}

static inline void
_draw_black (GstGLContext * context)
{
  const GstGLFuncs *gl = context->gl_vtable;

  gst_gl_insert_debug_marker (context, "no buffer.  rendering black");
  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT);
}

static gboolean
gtk_gst_gl_widget_render (GtkGLArea * widget, GdkGLContext * context)
{
  GtkGstGLWidgetPrivate *priv = GTK_GST_GL_WIDGET (widget)->priv;
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (widget);
  GstGLOverlayCompositor *overlay_compositor = NULL;

  GTK_GST_BASE_WIDGET_LOCK (widget);

  if (!priv->context || !priv->other_context)
    goto done;

  gst_gl_context_activate (priv->other_context, TRUE);

  if (!priv->initted)
    gtk_gst_gl_widget_init_redisplay (GTK_GST_GL_WIDGET (widget));

  if (!priv->initted || !base_widget->negotiated) {
    _draw_black (priv->other_context);
    goto done;
  }

  overlay_compositor = gst_gl_overlay_compositor_new (priv->other_context);

  /* Upload latest buffer */
  if (base_widget->pending_buffer) {
    GstBuffer *buffer = base_widget->pending_buffer;
    GstVideoFrame gl_frame;
    GstGLSyncMeta *sync_meta;

    if (!gst_video_frame_map (&gl_frame, &base_widget->v_info, buffer,
            GST_MAP_READ | GST_MAP_GL)) {
      _draw_black (priv->other_context);
      goto done;
    }

    priv->current_tex = *(guint *) gl_frame.data[0];
    gst_gl_insert_debug_marker (priv->other_context, "redrawing texture %u",
        priv->current_tex);

    gst_gl_overlay_compositor_upload_overlays (overlay_compositor, buffer);

    sync_meta = gst_buffer_get_gl_sync_meta (buffer);
    if (sync_meta) {
      /* XXX: the set_sync() seems to be needed for resizing */
      gst_gl_sync_meta_set_sync_point (sync_meta, priv->context);
      gst_gl_sync_meta_wait (sync_meta, priv->other_context);
    }

    gst_video_frame_unmap (&gl_frame);

    if (base_widget->buffer)
      gst_buffer_unref (base_widget->buffer);

    /* Keep the buffer to ensure current_tex stay valid */
    base_widget->buffer = buffer;
    base_widget->pending_buffer = NULL;
  }

  GST_DEBUG ("rendering buffer %p with gdk context %p",
      base_widget->buffer, context);

  _redraw_texture (GTK_GST_GL_WIDGET (widget), priv->current_tex);
  gst_gl_overlay_compositor_draw_overlays (overlay_compositor);

  gst_gl_insert_debug_marker (priv->other_context, "texture %u redrawn",
      priv->current_tex);

done:
  if (overlay_compositor)
    gst_object_unref (overlay_compositor);

  if (priv->other_context)
    gst_gl_context_activate (priv->other_context, FALSE);

  GTK_GST_BASE_WIDGET_UNLOCK (widget);
  return FALSE;
}

static void
_reset_gl (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  const GstGLFuncs *gl = priv->other_context->gl_vtable;

  if (!priv->gdk_context)
    priv->gdk_context = gtk_gl_area_get_context (GTK_GL_AREA (gst_widget));

  if (priv->gdk_context == NULL)
    return;

  gdk_gl_context_make_current (priv->gdk_context);
  gst_gl_context_activate (priv->other_context, TRUE);

  if (priv->vao) {
    gl->DeleteVertexArrays (1, &priv->vao);
    priv->vao = 0;
  }

  if (priv->vertex_buffer) {
    gl->DeleteBuffers (1, &priv->vertex_buffer);
    priv->vertex_buffer = 0;
  }

  if (priv->upload) {
    gst_object_unref (priv->upload);
    priv->upload = NULL;
  }

  if (priv->shader) {
    gst_object_unref (priv->shader);
    priv->shader = NULL;
  }

  gst_gl_context_activate (priv->other_context, FALSE);

  gst_object_unref (priv->other_context);
  priv->other_context = NULL;

  gdk_gl_context_clear_current ();

  g_object_unref (priv->gdk_context);
  priv->gdk_context = NULL;
}

static void
gtk_gst_gl_widget_finalize (GObject * object)
{
  GtkGstGLWidgetPrivate *priv = GTK_GST_GL_WIDGET (object)->priv;
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (object);

  if (priv->other_context)
    gst_gtk_invoke_on_main ((GThreadFunc) _reset_gl, base_widget);

  if (priv->context)
    gst_object_unref (priv->context);

  if (priv->display)
    gst_object_unref (priv->display);

  gtk_gst_base_widget_finalize (object);
  G_OBJECT_CLASS (gtk_gst_gl_widget_parent_class)->finalize (object);
}

static void
gtk_gst_gl_widget_class_init (GtkGstGLWidgetClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;
  GtkGLAreaClass *gl_widget_klass = (GtkGLAreaClass *) klass;

  gtk_gst_base_widget_class_init (GTK_GST_BASE_WIDGET_CLASS (klass));

  gobject_klass->finalize = gtk_gst_gl_widget_finalize;
  gl_widget_klass->render = gtk_gst_gl_widget_render;
}

static void
gtk_gst_gl_widget_init (GtkGstGLWidget * gst_widget)
{
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (gst_widget);
  GdkDisplay *display;
  GtkGstGLWidgetPrivate *priv;

  gtk_gst_base_widget_init (base_widget);

  gst_widget->priv = priv = gtk_gst_gl_widget_get_instance_private (gst_widget);

  display = gdk_display_get_default ();

#if GST_GL_HAVE_WINDOW_X11 && defined (GDK_WINDOWING_X11)
  if (GDK_IS_X11_DISPLAY (display)) {
    priv->display = (GstGLDisplay *)
        gst_gl_display_x11_new_with_display (gdk_x11_display_get_xdisplay
        (display));
  }
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND && defined (GDK_WINDOWING_WAYLAND)
  if (GDK_IS_WAYLAND_DISPLAY (display)) {
    struct wl_display *wayland_display =
        gdk_wayland_display_get_wl_display (display);
    priv->display = (GstGLDisplay *)
        gst_gl_display_wayland_new_with_display (wayland_display);
  }
#endif
#if GST_GL_HAVE_WINDOW_WIN32 && defined (GDK_WINDOWING_WIN32)
  if (GDK_IS_WIN32_DISPLAY (display)) {
    priv->display = (GstGLDisplay *)
        gst_gl_display_new_with_type (GST_GL_DISPLAY_TYPE_WIN32);
  }
#endif

  (void) display;

  if (!priv->display)
    priv->display = gst_gl_display_new ();

  GST_INFO ("Created %" GST_PTR_FORMAT, priv->display);

  gtk_gl_area_set_has_alpha (GTK_GL_AREA (gst_widget),
      !base_widget->ignore_alpha);
}

static void
_get_gl_context (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  GstGLPlatform platform = GST_GL_PLATFORM_NONE;
  GstGLAPI gl_api = GST_GL_API_NONE;
  guintptr gl_handle = 0;

  gtk_widget_realize (GTK_WIDGET (gst_widget));

  if (priv->other_context)
    gst_object_unref (priv->other_context);
  priv->other_context = NULL;

  if (priv->gdk_context)
    g_object_unref (priv->gdk_context);

  priv->gdk_context = gtk_gl_area_get_context (GTK_GL_AREA (gst_widget));
  if (priv->gdk_context == NULL) {
    GError *error = gtk_gl_area_get_error (GTK_GL_AREA (gst_widget));

    GST_ERROR_OBJECT (gst_widget, "Error creating GdkGLContext : %s",
        error ? error->message : "No error set by Gdk");
    return;
  }

  g_object_ref (priv->gdk_context);

  gdk_gl_context_make_current (priv->gdk_context);

#if GST_GL_HAVE_WINDOW_X11 && defined (GDK_WINDOWING_X11)
  if (GST_IS_GL_DISPLAY_X11 (priv->display)) {
#if GST_GL_HAVE_PLATFORM_GLX
    if (!gl_handle) {
      platform = GST_GL_PLATFORM_GLX;
      gl_handle = gst_gl_context_get_current_gl_context (platform);
    }
#endif

#if GST_GL_HAVE_PLATFORM_EGL
    if (!gl_handle) {
      platform = GST_GL_PLATFORM_EGL;
      gl_handle = gst_gl_context_get_current_gl_context (platform);
    }
#endif

    if (gl_handle) {
      gl_api = gst_gl_context_get_current_gl_api (platform, NULL, NULL);
      priv->other_context =
          gst_gl_context_new_wrapped (priv->display, gl_handle,
          platform, gl_api);
    }
  }
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND && GST_GL_HAVE_PLATFORM_EGL && defined (GDK_WINDOWING_WAYLAND)
  if (GST_IS_GL_DISPLAY_WAYLAND (priv->display)) {
    platform = GST_GL_PLATFORM_EGL;
    gl_api = gst_gl_context_get_current_gl_api (platform, NULL, NULL);
    gl_handle = gst_gl_context_get_current_gl_context (platform);
    if (gl_handle)
      priv->other_context =
          gst_gl_context_new_wrapped (priv->display, gl_handle,
          platform, gl_api);
  }
#endif
#if GST_GL_HAVE_WINDOW_WIN32 && (GST_GL_HAVE_PLATFORM_WGL || GST_GL_HAVE_PLATFORM_EGL) && defined (GDK_WINDOWING_WIN32)
  if (GDK_IS_WIN32_DISPLAY (gdk_gl_context_get_display (priv->gdk_context))) {
    gboolean is_gles = gdk_gl_context_get_use_es (priv->gdk_context);

    /* for WGL, we need the updates in GdkWin32GLContext for this to work safely */
    if (is_gles || GTK_CHECK_VERSION (3, 24, 43)) {
      platform = is_gles ? GST_GL_PLATFORM_EGL : GST_GL_PLATFORM_WGL;
      gl_api = gst_gl_context_get_current_gl_api (platform, NULL, NULL);
      gl_handle = gst_gl_context_get_current_gl_context (platform);

      if (gl_handle)
        priv->other_context =
            gst_gl_context_new_wrapped (priv->display, gl_handle,
            platform, gl_api);

      if (priv->other_context && !is_gles)
        priv->is_wgl = TRUE;
    }
  }
#endif

  (void) platform;
  (void) gl_api;
  (void) gl_handle;

  if (priv->other_context) {
    GError *error = NULL;

    GST_INFO ("Retrieved Gdk OpenGL context %" GST_PTR_FORMAT,
        priv->other_context);
    gst_gl_context_activate (priv->other_context, TRUE);
    if (!gst_gl_context_fill_info (priv->other_context, &error)) {
      GST_ERROR ("failed to retrieve gdk context info: %s", error->message);
      g_clear_error (&error);
      g_object_unref (priv->other_context);
      priv->other_context = NULL;
    } else {
      gst_gl_context_activate (priv->other_context, FALSE);
    }
  } else {
    GST_WARNING ("Could not retrieve Gdk OpenGL context");
  }
}

GtkWidget *
gtk_gst_gl_widget_new (void)
{
  return (GtkWidget *) g_object_new (GTK_TYPE_GST_GL_WIDGET, NULL);
}

gboolean
gtk_gst_gl_widget_init_winsys (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  GError *error = NULL;

  g_return_val_if_fail (GTK_IS_GST_GL_WIDGET (gst_widget), FALSE);
  g_return_val_if_fail (priv->display != NULL, FALSE);

  GTK_GST_BASE_WIDGET_LOCK (gst_widget);

  if (priv->display && priv->gdk_context && priv->other_context) {
    GST_TRACE ("have already initialized contexts");
    GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);
    return TRUE;
  }

  if (!priv->other_context) {
    GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);
    gst_gtk_invoke_on_main ((GThreadFunc) _get_gl_context, gst_widget);
    GTK_GST_BASE_WIDGET_LOCK (gst_widget);
  }

  if (!GST_IS_GL_CONTEXT (priv->other_context)) {
    GST_FIXME ("Could not retrieve Gdk OpenGL context");
    GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);
    return FALSE;
  }

  GST_OBJECT_LOCK (priv->display);

  /* If there's no wglCreateContextAttribsARB() support, then we would fallback to
   * wglShareLists() which will fail with ERROR_BUSY (0xaa) if either of the GL
   * contexts are current in any other thread.
   *
   * The workaround here is to temporarily disable GDK's GL context while we
   * set up our own, and then make the GDK GL Context current again after we are done
   *
   * Sometimes wglCreateContextAttribsARB()
   * exists, but isn't functional (some Intel drivers), so it's easiest to do this
   * unconditionally.
   */
  if (priv->gdk_context && priv->is_wgl)
    gdk_gl_context_clear_current ();

  if (!gst_gl_display_create_context (priv->display, priv->other_context,
          &priv->context, &error)) {
    GST_WARNING ("Could not create OpenGL context: %s",
        error ? error->message : "Unknown");
    g_clear_error (&error);
    GST_OBJECT_UNLOCK (priv->display);
    GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);
    return FALSE;
  }

  if (priv->gdk_context && priv->is_wgl)
    gdk_gl_context_make_current (priv->gdk_context);

  gst_gl_display_add_context (priv->display, priv->context);
  GST_OBJECT_UNLOCK (priv->display);

  GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);
  return TRUE;
}

GstGLContext *
gtk_gst_gl_widget_get_gtk_context (GtkGstGLWidget * gst_widget)
{
  if (!gst_widget->priv->other_context)
    return NULL;

  return gst_object_ref (gst_widget->priv->other_context);
}

GstGLContext *
gtk_gst_gl_widget_get_context (GtkGstGLWidget * gst_widget)
{
  if (!gst_widget->priv->context)
    return NULL;

  return gst_object_ref (gst_widget->priv->context);
}

GstGLDisplay *
gtk_gst_gl_widget_get_display (GtkGstGLWidget * gst_widget)
{
  if (!gst_widget->priv->display)
    return NULL;

  return gst_object_ref (gst_widget->priv->display);
}

void
gtk_gst_gl_widget_set_rotate_method (GtkGstGLWidget * gst_widget,
    GstVideoOrientationMethod method, gboolean from_tag)
{
  GstVideoOrientationMethod tag_method = GST_VIDEO_ORIENTATION_AUTO;
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;

  if (method == GST_VIDEO_ORIENTATION_CUSTOM) {
    GST_WARNING_OBJECT (gst_widget, "unsupported custom orientation");
    return;
  } else if (method == GST_VIDEO_ORIENTATION_AUTO && from_tag) {
    GST_WARNING_OBJECT (gst_widget, "auto orientation cannot come from a tag");
    return;
  }

  GTK_GST_BASE_WIDGET_LOCK (gst_widget);
  if (from_tag)
    tag_method = method;
  else
    priv->rotate_method = method;

  if (priv->rotate_method == GST_VIDEO_ORIENTATION_AUTO)
    method = tag_method;
  else
    method = priv->rotate_method;

  /* We can't apply an AUTO orientation if we don't have an
   * orientation coming from a tag, so reset to identity */
  if (method != priv->current_rotate_method &&
      method == GST_VIDEO_ORIENTATION_AUTO)
    method = GST_VIDEO_ORIENTATION_IDENTITY;

  if (method != priv->current_rotate_method) {
    GST_DEBUG ("Changing method from %d to %d",
        priv->current_rotate_method, method);

    switch (method) {
      case GST_VIDEO_ORIENTATION_IDENTITY:
        priv->transform_matrix = NULL;
        break;
      case GST_VIDEO_ORIENTATION_90R:
        priv->transform_matrix = clockwise_matrix;
        break;
      case GST_VIDEO_ORIENTATION_180:
        priv->transform_matrix = clockwise_180_matrix;
        break;
      case GST_VIDEO_ORIENTATION_90L:
        priv->transform_matrix = counterclockwise_matrix;
        break;
      case GST_VIDEO_ORIENTATION_HORIZ:
        priv->transform_matrix = horizontal_flip_matrix;
        break;
      case GST_VIDEO_ORIENTATION_VERT:
        priv->transform_matrix = vertical_flip_matrix;
        break;
      case GST_VIDEO_ORIENTATION_UL_LR:
        priv->transform_matrix = upper_left_matrix;
        break;
      case GST_VIDEO_ORIENTATION_UR_LL:
        priv->transform_matrix = upper_right_matrix;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    priv->current_rotate_method = method;
  }
  GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);

  gtk_gst_base_widget_queue_draw (GTK_GST_BASE_WIDGET (gst_widget));
}

GstVideoOrientationMethod
gtk_gst_gl_widget_get_rotate_method (GtkGstGLWidget * gst_widget)
{
  GtkGstGLWidgetPrivate *priv = gst_widget->priv;
  GstVideoOrientationMethod method;

  GTK_GST_BASE_WIDGET_LOCK (gst_widget);
  method = priv->current_rotate_method;
  GTK_GST_BASE_WIDGET_UNLOCK (gst_widget);

  return method;
}
