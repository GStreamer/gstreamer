/*
 *  gstvaapiwindow_egl.c - VA/EGL window abstraction
 *
 *  Copyright (C) 2014 Intel Corporation
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

/**
 * SECTION:gstvaapiwindow_egl
 * @short_description: VA/EGL window abstraction
 */

#include "sysdeps.h"
#include "gstvaapiwindow_egl.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapitexture_egl.h"
#include "gstvaapitexture_priv.h"
#include "gstvaapidisplay_egl_priv.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_vaapi_window);
#define GST_CAT_DEFAULT gst_debug_vaapi_window

#define GST_VAAPI_WINDOW_EGL_CAST(obj) \
    ((GstVaapiWindowEGL *)(obj))

#define GST_VAAPI_WINDOW_EGL_GET_PROXY(obj) \
    (GST_VAAPI_WINDOW_EGL_CAST(obj)->window)

#define GST_VAAPI_WINDOW_EGL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_WINDOW_EGL, GstVaapiWindowEGLClass))

typedef struct _GstVaapiWindowEGLClass GstVaapiWindowEGLClass;

enum
{
  RENDER_PROGRAM_VAR_PROJ = 0,
  RENDER_PROGRAM_VAR_TEX0,
  RENDER_PROGRAM_VAR_TEX1,
  RENDER_PROGRAM_VAR_TEX2,
};

struct _GstVaapiWindowEGL
{
  GstVaapiWindow parent_instance;

  GstVaapiWindow *window;
  GstVaapiTexture *texture;
  EglWindow *egl_window;
  EglVTable *egl_vtable;
  EglProgram *render_program;
  gfloat render_projection[16];
};

struct _GstVaapiWindowEGLClass
{
  GstVaapiWindowClass parent_class;
};

typedef struct
{
  GstVaapiWindowEGL *window;
  guint width;
  guint height;
  EglContext *egl_context;
  gboolean success;             /* result */
} CreateObjectsArgs;

typedef struct
{
  GstVaapiWindowEGL *window;
  guint width;
  guint height;
  gboolean success;             /* result */
} ResizeWindowArgs;

typedef struct
{
  GstVaapiWindowEGL *window;
  GstVaapiSurface *surface;
  const GstVaapiRectangle *src_rect;
  const GstVaapiRectangle *dst_rect;
  guint flags;
  gboolean success;             /* result */
} UploadSurfaceArgs;

/* *IDENT-OFF* */
static const gchar *vert_shader_text =
    "#ifdef GL_ES                                      \n"
    "precision mediump float;                          \n"
    "#endif                                            \n"
    "uniform mat4 proj;                                \n"
    "attribute vec2 position;                          \n"
    "attribute vec2 texcoord;                          \n"
    "varying vec2 v_texcoord;                          \n"
    "void main ()                                      \n"
    "{                                                 \n"
    "  gl_Position = proj * vec4 (position, 0.0, 1.0); \n"
    "  v_texcoord  = texcoord;                         \n"
    "}                                                 \n";

static const gchar *frag_shader_text_rgba =
    "#ifdef GL_ES                                      \n"
    "precision mediump float;                          \n"
    "#endif                                            \n"
    "uniform sampler2D tex0;                           \n"
    "varying vec2 v_texcoord;                          \n"
    "void main ()                                      \n"
    "{                                                 \n"
    "  gl_FragColor = texture2D (tex0, v_texcoord);    \n"
    "}                                                 \n";
/* *IDENT-ON* */

G_DEFINE_TYPE (GstVaapiWindowEGL, gst_vaapi_window_egl, GST_TYPE_VAAPI_WINDOW);

static gboolean
ensure_texture (GstVaapiWindowEGL * window, guint width, guint height)
{
  GstVaapiTexture *texture;

  if (window->texture &&
      GST_VAAPI_TEXTURE_WIDTH (window->texture) == width &&
      GST_VAAPI_TEXTURE_HEIGHT (window->texture) == height)
    return TRUE;

  texture = gst_vaapi_texture_egl_new (GST_VAAPI_WINDOW_DISPLAY (window),
      GL_TEXTURE_2D, GL_RGBA, width, height);
  gst_mini_object_replace ((GstMiniObject **) & window->texture,
      (GstMiniObject *) texture);
  gst_mini_object_replace ((GstMiniObject **) & texture, NULL);
  return window->texture != NULL;
}

static gboolean
ensure_shaders (GstVaapiWindowEGL * window)
{
  EglVTable *const vtable = window->egl_vtable;
  EglProgram *program;
  GLuint prog_id;

  g_return_val_if_fail (window->texture != NULL, FALSE);
  g_return_val_if_fail (GST_VAAPI_TEXTURE_FORMAT (window->texture) == GL_RGBA,
      FALSE);

  if (window->render_program)
    return TRUE;

  program = egl_program_new (window->egl_window->context,
      frag_shader_text_rgba, vert_shader_text);
  if (!program)
    return FALSE;

  prog_id = program->base.handle.u;

  vtable->glUseProgram (prog_id);
  program->uniforms[RENDER_PROGRAM_VAR_PROJ] =
      vtable->glGetUniformLocation (prog_id, "proj");
  program->uniforms[RENDER_PROGRAM_VAR_TEX0] =
      vtable->glGetUniformLocation (prog_id, "tex0");
  program->uniforms[RENDER_PROGRAM_VAR_TEX1] =
      vtable->glGetUniformLocation (prog_id, "tex1");
  program->uniforms[RENDER_PROGRAM_VAR_TEX2] =
      vtable->glGetUniformLocation (prog_id, "tex2");
  vtable->glUseProgram (0);

  egl_matrix_set_identity (window->render_projection);

  egl_object_replace (&window->render_program, program);
  egl_object_replace (&program, NULL);
  return TRUE;
}

static gboolean
do_create_objects_unlocked (GstVaapiWindowEGL * window, guint width,
    guint height, EglContext * egl_context)
{
  EglWindow *egl_window;
  EglVTable *egl_vtable;

  egl_window = egl_window_new (egl_context,
      GSIZE_TO_POINTER (GST_VAAPI_WINDOW_ID (GST_VAAPI_WINDOW_EGL_GET_PROXY
              (window))));
  if (!egl_window)
    return FALSE;
  window->egl_window = egl_window;

  egl_vtable = egl_context_get_vtable (egl_window->context, TRUE);
  if (!egl_vtable)
    return FALSE;
  window->egl_vtable = egl_object_ref (egl_vtable);
  return TRUE;
}

static void
do_create_objects (CreateObjectsArgs * args)
{
  GstVaapiWindowEGL *const window = args->window;
  EglContextState old_cs;

  args->success = FALSE;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  if (egl_context_set_current (args->egl_context, TRUE, &old_cs)) {
    args->success = do_create_objects_unlocked (window, args->width,
        args->height, args->egl_context);
    egl_context_set_current (args->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
}

static gboolean
gst_vaapi_window_egl_create (GstVaapiWindow * window, guint * width,
    guint * height)
{
  GstVaapiDisplayEGL *const display =
      GST_VAAPI_DISPLAY_EGL (GST_VAAPI_WINDOW_DISPLAY (window));
  const GstVaapiDisplayClass *const native_dpy_class =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);
  CreateObjectsArgs args;

  g_return_val_if_fail (native_dpy_class != NULL, FALSE);

  GST_VAAPI_WINDOW_EGL_GET_PROXY (window) =
      native_dpy_class->create_window (GST_VAAPI_DISPLAY (display->display),
      GST_VAAPI_ID_INVALID, *width, *height);
  if (!GST_VAAPI_WINDOW_EGL_GET_PROXY (window))
    return FALSE;

  gst_vaapi_window_get_size (GST_VAAPI_WINDOW_EGL_GET_PROXY (window), width,
      height);

  args.window = GST_VAAPI_WINDOW_EGL_CAST (window);
  args.width = *width;
  args.height = *height;
  args.egl_context = GST_VAAPI_DISPLAY_EGL_CONTEXT (display);
  return egl_context_run (args.egl_context,
      (EglContextRunFunc) do_create_objects, &args) && args.success;
}

static void
do_destroy_objects_unlocked (GstVaapiWindowEGL * window)
{
  egl_object_replace (&window->render_program, NULL);
  egl_object_replace (&window->egl_vtable, NULL);
  egl_object_replace (&window->egl_window, NULL);
}

static void
do_destroy_objects (GstVaapiWindowEGL * window)
{
  EglContext *const egl_context =
      GST_VAAPI_DISPLAY_EGL_CONTEXT (GST_VAAPI_WINDOW_DISPLAY (window));
  EglContextState old_cs;

  if (!window->egl_window)
    return;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  if (egl_context_set_current (egl_context, TRUE, &old_cs)) {
    do_destroy_objects_unlocked (window);
    egl_context_set_current (egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
}

static void
gst_vaapi_window_egl_finalize (GObject * object)
{
  GstVaapiWindowEGL *const window = GST_VAAPI_WINDOW_EGL (object);

  if (window->egl_window) {
    egl_context_run (window->egl_window->context,
        (EglContextRunFunc) do_destroy_objects, window);
  }

  gst_vaapi_window_replace (&window->window, NULL);
  gst_mini_object_replace ((GstMiniObject **) & window->texture, NULL);

  G_OBJECT_CLASS (gst_vaapi_window_egl_parent_class)->finalize (object);
}

static gboolean
gst_vaapi_window_egl_show (GstVaapiWindow * window)
{
  const GstVaapiWindowClass *const klass =
      GST_VAAPI_WINDOW_GET_CLASS (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));

  g_return_val_if_fail (klass->show, FALSE);

  return klass->show (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));
}

static gboolean
gst_vaapi_window_egl_hide (GstVaapiWindow * window)
{
  const GstVaapiWindowClass *const klass =
      GST_VAAPI_WINDOW_GET_CLASS (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));

  g_return_val_if_fail (klass->hide, FALSE);

  return klass->hide (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));
}

static gboolean
gst_vaapi_window_egl_get_geometry (GstVaapiWindow * window, gint * x_ptr,
    gint * y_ptr, guint * width_ptr, guint * height_ptr)
{
  const GstVaapiWindowClass *const klass =
      GST_VAAPI_WINDOW_GET_CLASS (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));

  return klass->get_geometry ?
      klass->get_geometry (GST_VAAPI_WINDOW_EGL_GET_PROXY (window), x_ptr,
      y_ptr, width_ptr, height_ptr) : FALSE;
}

static gboolean
gst_vaapi_window_egl_set_fullscreen (GstVaapiWindow * window,
    gboolean fullscreen)
{
  const GstVaapiWindowClass *const klass =
      GST_VAAPI_WINDOW_GET_CLASS (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));

  return klass->set_fullscreen ?
      klass->set_fullscreen (GST_VAAPI_WINDOW_EGL_GET_PROXY (window),
      fullscreen) : FALSE;
}

static gboolean
do_resize_window_unlocked (GstVaapiWindowEGL * window, guint width,
    guint height)
{
  EglVTable *const vtable = window->egl_vtable;

  vtable->glViewport (0, 0, width, height);
  vtable->glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
  vtable->glClear (GL_COLOR_BUFFER_BIT);
  return TRUE;
}

static void
do_resize_window (ResizeWindowArgs * args)
{
  GstVaapiWindowEGL *const window = args->window;
  EglContextState old_cs;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  if (egl_context_set_current (window->egl_window->context, TRUE, &old_cs)) {
    args->success = do_resize_window_unlocked (window, args->width,
        args->height);
    egl_context_set_current (window->egl_window->context, FALSE, &old_cs);
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
}

static gboolean
gst_vaapi_window_egl_resize (GstVaapiWindow * window, guint width, guint height)
{
  GstVaapiWindowEGL *const win = GST_VAAPI_WINDOW_EGL_CAST (window);
  const GstVaapiWindowClass *const klass =
      GST_VAAPI_WINDOW_GET_CLASS (GST_VAAPI_WINDOW_EGL_GET_PROXY (window));
  ResizeWindowArgs args = { win, width, height };

  g_return_val_if_fail (klass->resize, FALSE);

  if (!klass->resize (GST_VAAPI_WINDOW_EGL_GET_PROXY (window), width, height))
    return FALSE;

  return egl_context_run (win->egl_window->context,
      (EglContextRunFunc) do_resize_window, &args) && args.success;
}

static gboolean
do_render_texture (GstVaapiWindowEGL * window, const GstVaapiRectangle * rect)
{
  const GLuint tex_id = GST_VAAPI_TEXTURE_ID (window->texture);
  EglVTable *const vtable = window->egl_vtable;
  GLfloat x0, y0, x1, y1;
  GLfloat texcoords[4][2];
  GLfloat positions[4][2];
  guint tex_width, tex_height;

  if (!ensure_shaders (window))
    return FALSE;

  tex_width = GST_VAAPI_TEXTURE_WIDTH (window->texture);
  tex_height = GST_VAAPI_TEXTURE_HEIGHT (window->texture);

  // Source coords in VA surface
  x0 = 0.0f;
  y0 = 0.0f;
  x1 = 1.0f;
  y1 = 1.0f;
  texcoords[0][0] = x0;
  texcoords[0][1] = y1;
  texcoords[1][0] = x1;
  texcoords[1][1] = y1;
  texcoords[2][0] = x1;
  texcoords[2][1] = y0;
  texcoords[3][0] = x0;
  texcoords[3][1] = y0;

  // Target coords in EGL surface
  x0 = 2.0f * ((GLfloat) rect->x / tex_width) - 1.0f;
  y1 = -2.0f * ((GLfloat) rect->y / tex_height) + 1.0f;
  x1 = 2.0f * ((GLfloat) (rect->x + rect->width) / tex_width) - 1.0f;
  y0 = -2.0f * ((GLfloat) (rect->y + rect->height) / tex_height) + 1.0f;
  positions[0][0] = x0;
  positions[0][1] = y0;
  positions[1][0] = x1;
  positions[1][1] = y0;
  positions[2][0] = x1;
  positions[2][1] = y1;
  positions[3][0] = x0;
  positions[3][1] = y1;

  vtable->glClear (GL_COLOR_BUFFER_BIT);

  if (G_UNLIKELY (window->egl_window->context->config->gles_version == 1)) {
    vtable->glBindTexture (GST_VAAPI_TEXTURE_TARGET (window->texture), tex_id);
    vtable->glEnableClientState (GL_VERTEX_ARRAY);
    vtable->glVertexPointer (2, GL_FLOAT, 0, positions);
    vtable->glEnableClientState (GL_TEXTURE_COORD_ARRAY);
    vtable->glTexCoordPointer (2, GL_FLOAT, 0, texcoords);

    vtable->glDrawArrays (GL_TRIANGLE_FAN, 0, 4);

    vtable->glDisableClientState (GL_VERTEX_ARRAY);
    vtable->glDisableClientState (GL_TEXTURE_COORD_ARRAY);
  } else {
    EglProgram *const program = window->render_program;

    vtable->glUseProgram (program->base.handle.u);
    vtable->glUniformMatrix4fv (program->uniforms[RENDER_PROGRAM_VAR_PROJ],
        1, GL_FALSE, window->render_projection);
    vtable->glEnableVertexAttribArray (0);
    vtable->glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, positions);
    vtable->glEnableVertexAttribArray (1);
    vtable->glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

    vtable->glBindTexture (GST_VAAPI_TEXTURE_TARGET (window->texture), tex_id);
    vtable->glUniform1i (program->uniforms[RENDER_PROGRAM_VAR_TEX0], 0);
    vtable->glDrawArrays (GL_TRIANGLE_FAN, 0, 4);

    vtable->glDisableVertexAttribArray (1);
    vtable->glDisableVertexAttribArray (0);
    vtable->glUseProgram (0);
  }

  eglSwapBuffers (window->egl_window->context->display->base.handle.p,
      window->egl_window->base.handle.p);
  return TRUE;
}

static gboolean
do_upload_surface_unlocked (GstVaapiWindowEGL * window,
    GstVaapiSurface * surface, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  if (!ensure_texture (window, dst_rect->width, dst_rect->height))
    return FALSE;
  if (!gst_vaapi_texture_put_surface (window->texture, surface, src_rect,
          flags))
    return FALSE;
  if (!do_render_texture (window, dst_rect))
    return FALSE;
  return TRUE;
}

static void
do_upload_surface (UploadSurfaceArgs * args)
{
  GstVaapiWindowEGL *const window = args->window;
  EglContextState old_cs;

  args->success = FALSE;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  if (egl_context_set_current (window->egl_window->context, TRUE, &old_cs)) {
    args->success = do_upload_surface_unlocked (window, args->surface,
        args->src_rect, args->dst_rect, args->flags);
    egl_context_set_current (window->egl_window->context, FALSE, &old_cs);
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
}

static gboolean
gst_vaapi_window_egl_render (GstVaapiWindow * window, GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect, const GstVaapiRectangle * dst_rect,
    guint flags)
{
  GstVaapiWindowEGL *const win = GST_VAAPI_WINDOW_EGL_CAST (window);
  UploadSurfaceArgs args = { win, surface, src_rect, dst_rect, flags };

  return egl_context_run (win->egl_window->context,
      (EglContextRunFunc) do_upload_surface, &args) && args.success;
}

static void
gst_vaapi_window_egl_class_init (GstVaapiWindowEGLClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiWindowClass *const window_class = GST_VAAPI_WINDOW_CLASS (klass);

  object_class->finalize = gst_vaapi_window_egl_finalize;

  window_class->create = gst_vaapi_window_egl_create;
  window_class->show = gst_vaapi_window_egl_show;
  window_class->hide = gst_vaapi_window_egl_hide;
  window_class->get_geometry = gst_vaapi_window_egl_get_geometry;
  window_class->set_fullscreen = gst_vaapi_window_egl_set_fullscreen;
  window_class->resize = gst_vaapi_window_egl_resize;
  window_class->render = gst_vaapi_window_egl_render;
}

static void
gst_vaapi_window_egl_init (GstVaapiWindowEGL * window)
{
}

/**
 * gst_vaapi_window_egl_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels
 * @height: the requested windo height, in pixels
 *
 * Creates a window with the specified @width and @height. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_egl_new (GstVaapiDisplay * display, guint width, guint height)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), NULL);

  return gst_vaapi_window_new_internal (GST_TYPE_VAAPI_WINDOW_EGL, display,
      GST_VAAPI_ID_INVALID, width, height);
}
