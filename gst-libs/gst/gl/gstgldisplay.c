/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include "gstgldisplay.h"
#include "gstgldownload.h"
#include "gstglmemory.h"
#include "gstglfeature.h"

#ifndef GLEW_VERSION_MAJOR
#define GLEW_VERSION_MAJOR 4
#endif

#ifndef GLEW_VERSION_MINOR
#define GLEW_VERSION_MINOR 0
#endif

#define USING_OPENGL(display) (display->gl_api & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (display->gl_api & GST_GL_API_OPENGL3)
#define USING_GLES(display) (display->gl_api & GST_GL_API_GLES)
#define USING_GLES2(display) (display->gl_api & GST_GL_API_GLES2)
#define USING_GLES3(display) (display->gl_api & GST_GL_API_GLES3)

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display");

G_DEFINE_TYPE_WITH_CODE (GstGLDisplay, gst_gl_display, G_TYPE_OBJECT,
    DEBUG_INIT);

#define GST_GL_DISPLAY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_DISPLAY, GstGLDisplayPrivate))

static void gst_gl_display_finalize (GObject * object);

/* Called in the gl thread, protected by lock and unlock */
gpointer gst_gl_display_thread_create_context (GstGLDisplay * display);
void gst_gl_display_thread_destroy_context (GstGLDisplay * display);
void gst_gl_display_thread_run_generic (GstGLDisplay * display);
#if GST_GL_HAVE_GLES2
void gst_gl_display_thread_init_redisplay (GstGLDisplay * display);
#endif
void gst_gl_display_thread_gen_fbo (GstGLDisplay * display);
void gst_gl_display_thread_use_fbo (GstGLDisplay * display);
void gst_gl_display_thread_use_fbo_v2 (GstGLDisplay * display);
void gst_gl_display_thread_del_fbo (GstGLDisplay * display);
void gst_gl_display_thread_gen_shader (GstGLDisplay * display);
void gst_gl_display_thread_del_shader (GstGLDisplay * display);

/* private methods */
void gst_gl_display_lock (GstGLDisplay * display);
void gst_gl_display_unlock (GstGLDisplay * display);
void gst_gl_display_on_resize (GstGLDisplay * display, gint width, gint height);
void gst_gl_display_on_draw (GstGLDisplay * display);
void gst_gl_display_on_close (GstGLDisplay * display);
void gst_gl_display_del_texture_thread (GstGLDisplay * display,
    GLuint * pTexture);

void gst_gl_display_gen_texture_window_cb (GstGLDisplay * display);

#if GST_GL_HAVE_OPENGL
void _gen_fbo_opengl (GstGLDisplay * display);
void _use_fbo_opengl (GstGLDisplay * display);
void _use_fbo_v2_opengl (GstGLDisplay * display);
void _del_fbo_opengl (GstGLDisplay * display);
void _gen_shader_opengl (GstGLDisplay * display);
void _del_shader_opengl (GstGLDisplay * display);
#endif
#if GST_GL_HAVE_GLES2
void _gen_fbo_gles2 (GstGLDisplay * display);
void _use_fbo_gles2 (GstGLDisplay * display);
void _use_fbo_v2_gles2 (GstGLDisplay * display);
void _del_fbo_gles2 (GstGLDisplay * display);
void _gen_shader_gles2 (GstGLDisplay * display);
void _del_shader_gles2 (GstGLDisplay * display);
#endif

typedef struct _GstGLVtable
{
  GstGLWindowCB gen_fbo;
  GstGLWindowCB use_fbo;
  GstGLWindowCB use_fbo_v2;
  GstGLWindowCB del_fbo;
  GstGLWindowCB gen_shader;
  GstGLWindowCB del_shader;
} GstGLVtable;

#if GST_GL_HAVE_GLES2
/* *INDENT-OFF* */
static const gchar *redisplay_vertex_shader_str_gles2 =
      "attribute vec4 a_position;   \n"
      "attribute vec2 a_texCoord;   \n"
      "varying vec2 v_texCoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position; \n"
      "   v_texCoord = a_texCoord;  \n"
      "}                            \n";

static const gchar *redisplay_fragment_shader_str_gles2 =
      "precision mediump float;                            \n"
      "varying vec2 v_texCoord;                            \n"
      "uniform sampler2D s_texture;                        \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
      "}                                                   \n";
/* *INDENT-ON* */
#endif

struct _GstGLDisplayPrivate
{
  /* conditions */
  GCond *cond_create_context;
  GCond *cond_destroy_context;

  /* generic gl code */
  GstGLDisplayThreadFunc generic_callback;
  gpointer data;

  /* action redisplay */
  GLuint redisplay_texture;
  GLuint redisplay_texture_width;
  GLuint redisplay_texture_height;
#if GST_GL_HAVE_GLES2
  GstGLShader *redisplay_shader;
  gchar *redisplay_vertex_shader_str_gles2;
  gchar *redisplay_fragment_shader_str_gles2;
  GLint redisplay_attr_position_loc;
  GLint redisplay_attr_texture_loc;
#endif

  /* action gen and del texture */
  GLuint gen_texture;
  GLuint gen_texture_width;
  GLuint gen_texture_height;
  GstVideoFormat gen_texture_video_format;

  /* client callbacks */
  CRCB clientReshapeCallback;
  CDCB clientDrawCallback;
  gpointer client_data;

  /* filter gen fbo */
  GLuint gen_fbo_width;
  GLuint gen_fbo_height;
  GLuint generated_fbo;
  GLuint generated_depth_buffer;

  /* filter use fbo */
  GLuint use_fbo;
  GLuint use_depth_buffer;
  GLuint use_fbo_texture;
  GLuint use_fbo_width;
  GLuint use_fbo_height;
  GLCB use_fbo_scene_cb;
  GLCB_V2 use_fbo_scene_cb_v2;
  gdouble use_fbo_proj_param1;
  gdouble use_fbo_proj_param2;
  gdouble use_fbo_proj_param3;
  gdouble use_fbo_proj_param4;
  GstGLDisplayProjection use_fbo_projection;
  gpointer *use_fbo_stuff;
  GLuint input_texture_width;
  GLuint input_texture_height;
  GLuint input_texture;

  /* filter del fbo */
  GLuint del_fbo;
  GLuint del_depth_buffer;

  /* action gen and del shader */
  const gchar *gen_shader_fragment_source;
  const gchar *gen_shader_vertex_source;
  GstGLShader *gen_shader;
  GstGLShader *del_shader;

  GstGLVtable vtable;
};

/*------------------------------------------------------------
  --------------------- For klass GstGLDisplay ---------------
  ----------------------------------------------------------*/
static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLDisplayPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}


static void
gst_gl_display_init (GstGLDisplay * display)
{
  display->priv = GST_GL_DISPLAY_GET_PRIVATE (display);

  /* thread safe */
  display->mutex = g_mutex_new ();

  display->priv->cond_create_context = g_cond_new ();
  display->priv->cond_destroy_context = g_cond_new ();

  gst_gl_memory_init ();
}

static void
gst_gl_display_finalize (GObject * object)
{
  GstGLDisplay *display = GST_GL_DISPLAY (object);

  if (display->mutex && display->gl_window) {

    gst_gl_display_lock (display);

    gst_gl_window_set_resize_callback (display->gl_window, NULL, NULL);
    gst_gl_window_set_draw_callback (display->gl_window, NULL, NULL);
    gst_gl_window_set_close_callback (display->gl_window, NULL, NULL);

    GST_INFO ("send quit gl window loop");

    gst_gl_window_quit (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_thread_destroy_context), display);

    GST_INFO ("quit sent to gl window loop");

    g_cond_wait (display->priv->cond_destroy_context, display->mutex);
    GST_INFO ("quit received from gl window");
    gst_gl_display_unlock (display);
  }

  if (display->gl_thread) {
    gpointer ret = g_thread_join (display->gl_thread);
    GST_INFO ("gl thread joined");
    if (ret != NULL)
      GST_ERROR ("gl thread returned a not null pointer");
    display->gl_thread = NULL;
  }
  if (display->mutex) {
    g_mutex_free (display->mutex);
    display->mutex = NULL;
  }
  if (display->priv->cond_destroy_context) {
    g_cond_free (display->priv->cond_destroy_context);
    display->priv->cond_destroy_context = NULL;
  }
  if (display->priv->cond_create_context) {
    g_cond_free (display->priv->cond_create_context);
    display->priv->cond_create_context = NULL;
  }
  if (display->priv->clientReshapeCallback)
    display->priv->clientReshapeCallback = NULL;
  if (display->priv->clientDrawCallback)
    display->priv->clientDrawCallback = NULL;
  if (display->priv->client_data)
    display->priv->client_data = NULL;
  if (display->priv->use_fbo_scene_cb)
    display->priv->use_fbo_scene_cb = NULL;
  if (display->priv->use_fbo_scene_cb_v2)
    display->priv->use_fbo_scene_cb_v2 = NULL;
  if (display->priv->use_fbo_stuff)
    display->priv->use_fbo_stuff = NULL;

  if (display->error_message) {
    g_free (display->error_message);
    display->error_message = NULL;
  }
  if (display->uploads) {
    g_slist_free_full (display->uploads, g_object_unref);
    display->uploads = NULL;
  }
  if (display->downloads) {
    g_slist_free_full (display->downloads, g_object_unref);
    display->downloads = NULL;
  }
}


//------------------------------------------------------------
//------------------ BEGIN GL THREAD PROCS -------------------
//------------------------------------------------------------

/* Called in the gl thread */

void
gst_gl_display_set_error (GstGLDisplay * display, const char *format, ...)
{
  va_list args;

  if (display->error_message)
    g_free (display->error_message);

  va_start (args, format);
  display->error_message = g_strdup_vprintf (format, args);
  va_end (args);

  GST_WARNING (display->error_message);

  display->isAlive = FALSE;
}

#if GST_GL_HAVE_GLES2
static gboolean
_create_context_gles2 (GstGLDisplay * display, gint * gl_major, gint * gl_minor)
{
  GLenum gl_err = GL_NO_ERROR;

  GST_INFO ("GL_VERSION: %s", glGetString (GL_VERSION));
  GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
      glGetString (GL_SHADING_LANGUAGE_VERSION));
  GST_INFO ("GL_VENDOR: %s", glGetString (GL_VENDOR));
  GST_INFO ("GL_RENDERER: %s", glGetString (GL_RENDERER));

  gl_err = glGetError ();
  if (gl_err != GL_NO_ERROR) {
    gst_gl_display_set_error (display, "glGetString error: 0x%x", gl_err);
  }

  if (!GL_ES_VERSION_2_0)
    gst_gl_display_set_error (display, "OpenGL|ES >= 2.0 is required");

  _gst_gl_feature_check_ext_functions (display, 0, 0,
      glGetString (GL_EXTENSIONS));

  display->priv->vtable.gen_fbo = (GstGLWindowCB) _gen_fbo_gles2;
  display->priv->vtable.use_fbo = (GstGLWindowCB) _use_fbo_gles2;
  display->priv->vtable.use_fbo_v2 = (GstGLWindowCB) _use_fbo_v2_gles2;
  display->priv->vtable.del_fbo = (GstGLWindowCB) _del_fbo_gles2;
  display->priv->vtable.gen_shader = (GstGLWindowCB) _gen_shader_gles2;
  display->priv->vtable.del_shader = (GstGLWindowCB) _del_shader_gles2;

  if (gl_major)
    *gl_major = 2;
  if (gl_minor)
    *gl_minor = 0;

  return TRUE;
}
#endif

#if GST_GL_HAVE_OPENGL
gboolean
_create_context_opengl (GstGLDisplay * display, gint * gl_major,
    gint * gl_minor)
{
  guint maj, min;
  GLenum gl_err = GL_NO_ERROR;
  GLenum err;
  GString *opengl_version = NULL;

  if ((err = glewInit ()) != GLEW_OK) {
    gst_gl_display_set_error (display, "Failed to init GLEW: %s",
        glewGetErrorString (err));
    return TRUE;
  }

  /* OpenGL > 1.2.0 and Glew > 1.4.0 */
  GST_INFO ("GL_VERSION: %s", glGetString (GL_VERSION));
  GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
      glGetString (GL_SHADING_LANGUAGE_VERSION));
  GST_INFO ("GL_VENDOR: %s", glGetString (GL_VENDOR));
  GST_INFO ("GL_RENDERER: %s", glGetString (GL_RENDERER));

  gl_err = glGetError ();
  if (gl_err != GL_NO_ERROR) {
    gst_gl_display_set_error (display, "glGetString error: 0x%x", gl_err);
  }
  opengl_version =
      g_string_truncate (g_string_new ((gchar *) glGetString (GL_VERSION)), 3);

  sscanf (opengl_version->str, "%d.%d", &maj, &min);

  g_string_free (opengl_version, TRUE);

  if ((maj < 1) || (maj < 2 && maj >= 1 && min < 2)) {
    /* turn off the pipeline, the old drivers are not yet supported */
    gst_gl_display_set_error (display,
        "OpenGL >= 1.2.0 and Glew >= 1.4.0 is required");
    return FALSE;
  }

  _gst_gl_feature_check_ext_functions (display, maj, min,
      (const gchar *) glGetString (GL_EXTENSIONS));

  display->priv->vtable.gen_fbo = (GstGLWindowCB) _gen_fbo_opengl;
  display->priv->vtable.use_fbo = (GstGLWindowCB) _use_fbo_opengl;
  display->priv->vtable.use_fbo_v2 = (GstGLWindowCB) _use_fbo_v2_opengl;
  display->priv->vtable.del_fbo = (GstGLWindowCB) _del_fbo_opengl;
  display->priv->vtable.gen_shader = (GstGLWindowCB) _gen_shader_opengl;
  display->priv->vtable.del_shader = (GstGLWindowCB) _del_shader_opengl;

  if (gl_major)
    *gl_major = maj;
  if (gl_minor)
    *gl_minor = min;

  return TRUE;
}
#endif

GstGLAPI
_compiled_api (void)
{
  GstGLAPI ret = GST_GL_API_NONE;

#if GST_GL_HAVE_OPENGL
  ret |= GST_GL_API_OPENGL;
#endif
#if GST_GL_HAVE_GLES2
  ret |= GST_GL_API_GLES2;
#endif

  return ret;
}

gpointer
gst_gl_display_thread_create_context (GstGLDisplay * display)
{
  gint gl_major = 0;
  gboolean ret = FALSE;
  GError *error = NULL;
  GstGLAPI compiled_api;
  gchar *api_string;
  gchar *compiled_api_s;

  gst_gl_display_lock (display);

  compiled_api = _compiled_api ();

  display->gl_window =
      gst_gl_window_new (compiled_api, display->external_gl_context, &error);

  if (!display->gl_window || error) {
    gst_gl_display_set_error (display,
        error ? error->message : "Failed to create gl window");
    goto failure;
  }

  GST_INFO ("gl window created");

  display->gl_api = gst_gl_window_get_gl_api (display->gl_window);
  g_assert (display->gl_api != GST_GL_API_NONE
      && display->gl_api != GST_GL_API_ANY);

  api_string = gst_gl_api_string (display->gl_api);
  GST_INFO ("available GL APIs: %s", api_string);

  compiled_api_s = gst_gl_api_string (compiled_api);
  GST_INFO ("compiled api support: %s", compiled_api_s);

  if ((compiled_api & display->gl_api) == GST_GL_API_NONE) {
    gst_gl_display_set_error (display, "failed to create_context, window "
        "could not provide correct api. compiled api supports:%s, window "
        "supports:%s", compiled_api_s, api_string);
    goto failure;
  }

  g_free (api_string);
  g_free (compiled_api_s);

  /* gl api specific code */
#if GST_GL_HAVE_OPENGL
  if (!ret && USING_OPENGL (display))
    ret = _create_context_opengl (display, &gl_major, NULL);
#endif
#if GST_GL_HAVE_GLES2
  if (!ret && USING_GLES2 (display))
    ret = _create_context_gles2 (display, &gl_major, NULL);
#endif

  if (!ret || !gl_major) {
    gst_gl_display_set_error (display,
        "failed to create context, unknown reason");
    goto failure;
  }

  /* setup callbacks */
  gst_gl_window_set_resize_callback (display->gl_window,
      GST_GL_WINDOW_RESIZE_CB (gst_gl_display_on_resize), display);
  gst_gl_window_set_draw_callback (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_on_draw), display);
  gst_gl_window_set_close_callback (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_on_close), display);

  g_cond_signal (display->priv->cond_create_context);

  display->isAlive = TRUE;
  gst_gl_display_unlock (display);

  gst_gl_window_run (display->gl_window);

  GST_INFO ("loop exited\n");

  gst_gl_display_lock (display);

  display->isAlive = FALSE;

  g_object_unref (G_OBJECT (display->gl_window));

  display->gl_window = NULL;

  g_cond_signal (display->priv->cond_destroy_context);

  gst_gl_display_unlock (display);

  return NULL;

failure:
  {
    if (display->gl_window) {
      g_object_unref (display->gl_window);
      display->gl_window = NULL;
    }

    g_cond_signal (display->priv->cond_create_context);
    gst_gl_display_unlock (display);
    return NULL;
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_destroy_context (GstGLDisplay * display)
{
#if GST_GL_HAVE_GLES2
  if (display->redisplay_shader) {
    g_object_unref (G_OBJECT (display->redisplay_shader));
    display->redisplay_shader = NULL;
  }
#endif

  GST_INFO ("Context destroyed");
}


void
gst_gl_display_thread_run_generic (GstGLDisplay * display)
{
  GST_TRACE ("running function:%p data:%p",
      display->priv->generic_callback, display->priv->data);

  display->priv->generic_callback (display, display->priv->data);
}

#if GST_GL_HAVE_GLES2
/* Called in the gl thread */
void
gst_gl_display_thread_init_redisplay (GstGLDisplay * display)
{
  GError *error = NULL;
  display->priv->redisplay_shader = gst_gl_shader_new (display);

  gst_gl_shader_set_vertex_source (display->priv->redisplay_shader,
      redisplay_vertex_shader_str_gles2);
  gst_gl_shader_set_fragment_source (display->priv->redisplay_shader,
      redisplay_fragment_shader_str_gles2);

  gst_gl_shader_compile (display->priv->redisplay_shader, &error);
  if (error) {
    gst_gl_display_set_error (display, "%s", error->message);
    g_error_free (error);
    error = NULL;
    gst_gl_shader_use (NULL);
  } else {
    display->priv->redisplay_attr_position_loc =
        gst_gl_shader_get_attribute_location (display->priv->redisplay_shader,
        "a_position");
    display->priv->redisplay_attr_texture_loc =
        gst_gl_shader_get_attribute_location (display->priv->redisplay_shader,
        "a_texCoord");
  }
}
#endif

#if GST_GL_HAVE_OPENGL
void
_gen_fbo_opengl (GstGLDisplay * display)
{
  /* a texture must be attached to the FBO */
  GLuint fake_texture = 0;

  GST_TRACE ("creating FBO dimensions:%ux%u", display->priv->gen_fbo_width,
      display->priv->gen_fbo_height);

  /* -- generate frame buffer object */

  if (!GLEW_EXT_framebuffer_object) {
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object not supported");
    return;
  }
  /* setup FBO */
  glGenFramebuffersEXT (1, &display->priv->generated_fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->priv->generated_fbo);

  /* setup the render buffer for depth */
  glGenRenderbuffersEXT (1, &display->priv->generated_depth_buffer);
  glBindRenderbufferEXT (GL_RENDERBUFFER_EXT,
      display->priv->generated_depth_buffer);

  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
      display->priv->gen_fbo_width, display->priv->gen_fbo_height);
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT,
      display->priv->gen_fbo_width, display->priv->gen_fbo_height);

  /* setup a texture to render to */
  glGenTextures (1, &fake_texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
      display->priv->gen_fbo_width, display->priv->gen_fbo_height, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, display->priv->generated_depth_buffer);

  if (USING_OPENGL (display))
    glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT,
        GL_RENDERBUFFER_EXT, display->priv->generated_depth_buffer);

  if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) !=
      GL_FRAMEBUFFER_COMPLETE_EXT)
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");

  /* unbind the FBO */
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  glDeleteTextures (1, &fake_texture);
}
#endif

#if GST_GL_HAVE_GLES2
void
_gen_fbo_gles2 (GstGLDisplay * display)
{
  /* a texture must be attached to the FBO */
  GLuint fake_texture = 0;

  GST_TRACE ("creating FBO dimensions:%ux%u", display->priv->gen_fbo_width,
      display->priv->gen_fbo_height);

  /* -- generate frame buffer object */

  if (!GLEW_EXT_framebuffer_object) {
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object not supported");
    return;
  }
  /* setup FBO */
  glGenFramebuffersEXT (1, &display->priv->generated_fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->priv->generated_fbo);

  /* setup the render buffer for depth */
  glGenRenderbuffersEXT (1, &display->priv->generated_depth_buffer);
  glBindRenderbufferEXT (GL_RENDERBUFFER_EXT,
      display->priv->generated_depth_buffer);

  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16,
      display->priv->gen_fbo_width, display->priv->gen_fbo_height);

  /* setup a texture to render to */
  glGenTextures (1, &fake_texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
      display->priv->gen_fbo_width, display->priv->gen_fbo_height, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, display->priv->generated_depth_buffer);

  if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) !=
      GL_FRAMEBUFFER_COMPLETE_EXT)
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");

  /* unbind the FBO */
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  glDeleteTextures (1, &fake_texture);
}
#endif

/* Called in the gl thread */
#if GST_GL_HAVE_OPENGL
void
_use_fbo_opengl (GstGLDisplay * display)
{
  GST_TRACE ("Binding v1 FBO %u dimensions:%ux%u with texture:%u "
      "dimensions:%ux%u", display->priv->use_fbo, display->priv->use_fbo_width,
      display->priv->use_fbo_height, display->priv->use_fbo_texture,
      display->priv->input_texture_width, display->priv->input_texture_height);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->priv->use_fbo);

  /* setup a texture to render to */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture, 0);

  if (GLEW_ARB_fragment_shader)
    gst_gl_shader_use (NULL);

  glPushAttrib (GL_VIEWPORT_BIT);
  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();

  switch (display->priv->use_fbo_projection) {
    case GST_GL_DISPLAY_PROJECTION_ORTHO2D:
      gluOrtho2D (display->priv->use_fbo_proj_param1,
          display->priv->use_fbo_proj_param2,
          display->priv->use_fbo_proj_param3,
          display->priv->use_fbo_proj_param4);
      break;
    case GST_GL_DISPLAY_PROJECTION_PERSPECTIVE:
      gluPerspective (display->priv->use_fbo_proj_param1,
          display->priv->use_fbo_proj_param2,
          display->priv->use_fbo_proj_param3,
          display->priv->use_fbo_proj_param4);
      break;
    default:
      gst_gl_display_set_error (display, "Unknow fbo projection %d",
          display->priv->use_fbo_projection);
  }

  glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();


  glViewport (0, 0, display->priv->use_fbo_width,
      display->priv->use_fbo_height);

  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* the opengl scene */
  display->priv->use_fbo_scene_cb (display->priv->input_texture_width,
      display->priv->input_texture_height, display->priv->input_texture,
      display->priv->use_fbo_stuff);

  glDrawBuffer (GL_NONE);
  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);
  glPopMatrix ();
  glPopAttrib ();

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}
#endif

#if GST_GL_HAVE_GLES2
void
_use_fbo_gles2 (GstGLDisplay * display)
{
  GLint viewport_dim[4];

  GST_TRACE ("Binding v1 FBO %u dimensions:%ux%u with texture:%u "
      "dimensions:%ux%u", display->priv->use_fbo, display->priv->use_fbo_width,
      display->priv->use_fbo_height, display->priv->use_fbo_texture,
      display->priv->input_texture_width, display->priv->input_texture_height);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->priv->use_fbo);

  /* setup a texture to render to */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture, 0);

  glGetIntegerv (GL_VIEWPORT, viewport_dim);

  glViewport (0, 0, display->use_fbo_width, display->use_fbo_height);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* the opengl scene */
  display->priv->use_fbo_scene_cb (display->priv->input_texture_width,
      display->priv->input_texture_height, display->priv->input_texture,
      display->priv->use_fbo_stuff);

  glViewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}
#endif

/* Called in a gl thread
 * Need full shader support */
#if GST_GL_HAVE_OPENGL
void
_use_fbo_v2_opengl (GstGLDisplay * display)
{
  GLint viewport_dim[4];

  GST_TRACE ("Binding v2 FBO %u dimensions:%ux%u with texture:%u ",
      display->priv->use_fbo, display->priv->use_fbo_width,
      display->priv->use_fbo_height, display->priv->use_fbo_texture);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->priv->use_fbo);

  /* setup a texture to render to */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture, 0);

  glGetIntegerv (GL_VIEWPORT, viewport_dim);

  glViewport (0, 0, display->priv->use_fbo_width,
      display->priv->use_fbo_height);

  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* the opengl scene */
  display->priv->use_fbo_scene_cb_v2 (display->priv->use_fbo_stuff);

  glDrawBuffer (GL_NONE);

  glViewport (viewport_dim[0], viewport_dim[1],
      viewport_dim[2], viewport_dim[3]);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}
#endif

#if GST_GL_HAVE_GLES2
void
_use_fbo_v2_gles2 (GstGLDisplay * display)
{
  GLint viewport_dim[4];

  GST_TRACE ("Binding v2 FBO %u dimensions:%ux%u with texture:%u ",
      display->priv->use_fbo, display->priv->use_fbo_width,
      display->priv->use_fbo_height, display->priv->use_fbo_texture);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->priv->use_fbo);

  /* setup a texture to render to */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, display->priv->use_fbo_texture, 0);

  glGetIntegerv (GL_VIEWPORT, viewport_dim);

  glViewport (0, 0, display->priv->use_fbo_width,
      display->priv->use_fbo_height);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  //the opengl scene
  display->priv->use_fbo_scene_cb_v2 (display->priv->use_fbo_stuff);

  glViewport (viewport_dim[0], viewport_dim[1],
      viewport_dim[2], viewport_dim[3]);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}
#endif

/* Called in the gl thread */
#if GST_GL_HAVE_OPENGL
void
_del_fbo_opengl (GstGLDisplay * display)
{
  GST_TRACE ("Deleting FBO %u", display->priv->del_fbo);

  if (display->priv->del_fbo) {
    glDeleteFramebuffersEXT (1, &display->priv->del_fbo);
    display->priv->del_fbo = 0;
  }
  if (display->priv->del_depth_buffer) {
    glDeleteRenderbuffersEXT (1, &display->priv->del_depth_buffer);
    display->priv->del_depth_buffer = 0;
  }
}
#endif

#if GST_GL_HAVE_GLES2
void
_del_fbo_gles2 (GstGLDisplay * display)
{
  GST_TRACE ("Deleting FBO %u", display->priv->del_fbo);

  if (display->priv->del_fbo) {
    glDeleteFramebuffersEXT (1, &display->priv->del_fbo);
    display->priv->del_fbo = 0;
  }
  if (display->del_depth_buffer) {
    glDeleteRenderbuffersEXT (1, &display->priv->del_depth_buffer);
    display->priv->del_depth_buffer = 0;
  }
}
#endif

#if GST_GL_HAVE_OPENGL
/* Called in the gl thread */
void
_gen_shader_opengl (GstGLDisplay * display)
{
  GST_TRACE ("Generating shader %" GST_PTR_FORMAT, display->priv->gen_shader);

  if (GLEW_ARB_fragment_shader) {
    if (display->priv->gen_shader_vertex_source ||
        display->priv->gen_shader_fragment_source) {
      GError *error = NULL;

      display->priv->gen_shader = gst_gl_shader_new (display);

      if (display->priv->gen_shader_vertex_source)
        gst_gl_shader_set_vertex_source (display->priv->gen_shader,
            display->priv->gen_shader_vertex_source);

      if (display->priv->gen_shader_fragment_source)
        gst_gl_shader_set_fragment_source (display->priv->gen_shader,
            display->priv->gen_shader_fragment_source);

      gst_gl_shader_compile (display->priv->gen_shader, &error);
      if (error) {
        gst_gl_display_set_error (display, "%s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_shader_use (NULL);
        g_object_unref (G_OBJECT (display->priv->gen_shader));
        display->priv->gen_shader = NULL;
      }
    }
  } else {
    gst_gl_display_set_error (display,
        "One of the filter required ARB_fragment_shader");
    display->priv->gen_shader = NULL;
  }
}
#endif

#if GST_GL_HAVE_GLES2
void
_gen_shader_gles2 (GstGLDisplay * display)
{
  GST_TRACE ("Generating shader %" GST_PTR_FORMAT, display->priv->gen_shader);

  if (GLEW_ARB_fragment_shader) {
    if (display->priv->gen_shader_vertex_source ||
        display->priv->gen_shader_fragment_source) {
      GError *error = NULL;

      display->priv->gen_shader = gst_gl_shader_new (display);

      if (display->priv->gen_shader_vertex_source)
        gst_gl_shader_set_vertex_source (display->priv->gen_shader,
            display->priv->gen_shader_vertex_source);

      if (display->priv->gen_shader_fragment_source)
        gst_gl_shader_set_fragment_source (display->priv->gen_shader,
            display->priv->gen_shader_fragment_source);

      gst_gl_shader_compile (display->priv->gen_shader, &error);
      if (error) {
        gst_gl_display_set_error (display, "%s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_shader_use (NULL);
        g_object_unref (G_OBJECT (display->priv->gen_shader));
        display->priv->gen_shader = NULL;
      }
    }
  } else {
    gst_gl_display_set_error (display,
        "One of the filter required ARB_fragment_shader");
    display->priv->gen_shader = NULL;
  }
}
#endif

/* Called in the gl thread */
#if GST_GL_HAVE_OPENGL
void
_del_shader_opengl (GstGLDisplay * display)
{
  GST_TRACE ("Deleting shader %" GST_PTR_FORMAT, display->priv->del_shader);

  if (display->priv->del_shader) {
    g_object_unref (G_OBJECT (display->priv->del_shader));
    display->priv->del_shader = NULL;
  }
}
#endif

#if GST_GL_HAVE_GLES2
void
_del_shader_gles2 (GstGLDisplay * display)
{
  GST_TRACE ("Deleting shader %" GST_PTR_FORMAT, display->priv->del_shader);

  if (display->priv->del_shader) {
    g_object_unref (G_OBJECT (display->priv->del_shader));
    display->del_shader = NULL;
  }
}
#endif

//------------------------------------------------------------
//------------------ BEGIN GL THREAD ACTIONS -----------------
//------------------------------------------------------------


//------------------------------------------------------------
//---------------------- BEGIN PRIVATE -----------------------
//------------------------------------------------------------


void
gst_gl_display_on_resize (GstGLDisplay * display, gint width, gint height)
{
  GST_TRACE ("GL Window resized to %ux%u", width, height);

  //check if a client reshape callback is registered
  if (display->priv->clientReshapeCallback)
    display->priv->clientReshapeCallback (width, height,
        display->priv->client_data);

  //default reshape
  else {
    if (display->keep_aspect_ratio) {
      GstVideoRectangle src, dst, result;

      src.x = 0;
      src.y = 0;
      src.w = display->priv->redisplay_texture_width;
      src.h = display->priv->redisplay_texture_height;

      dst.x = 0;
      dst.y = 0;
      dst.w = width;
      dst.h = height;

      gst_video_sink_center_rect (src, dst, &result, TRUE);
      glViewport (result.x, result.y, result.w, result.h);
    } else {
      glViewport (0, 0, width, height);
    }
#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (display)) {
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();
      gluOrtho2D (0, width, 0, height);
      glMatrixMode (GL_MODELVIEW);
    }
#endif
  }
}


void
gst_gl_display_on_draw (GstGLDisplay * display)
{
  /* check if texture is ready for being drawn */
  if (!display->priv->redisplay_texture)
    return;

  /* opengl scene */
  GST_TRACE ("on draw");

  /* make sure that the environnement is clean */
  if (display->colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
    glUseProgramObjectARB (0);

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (display))
    glDisable (GL_TEXTURE_RECTANGLE_ARB);
#endif

  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  /* check if a client draw callback is registered */
  if (display->priv->clientDrawCallback) {
    gboolean doRedisplay =
        display->priv->clientDrawCallback (display->priv->redisplay_texture,
        display->priv->redisplay_texture_width,
        display->priv->redisplay_texture_height,
        display->priv->client_data);

    if (doRedisplay && display->gl_window)
      gst_gl_window_draw_unlocked (display->gl_window,
          display->priv->redisplay_texture_width,
          display->priv->redisplay_texture_height);
  }
  /* default opengl scene */
  else {
#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (display)) {
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
          display->priv->redisplay_texture);
      glEnable (GL_TEXTURE_RECTANGLE_ARB);

      glBegin (GL_QUADS);
      /* gst images are top-down while opengl plane is bottom-up */
      glTexCoord2i (display->priv->redisplay_texture_width, 0);
      glVertex2f (1.0f, 1.0f);
      glTexCoord2i (0, 0);
      glVertex2f (-1.0f, 1.0f);
      glTexCoord2i (0, display->priv->redisplay_texture_height);
      glVertex2f (-1.0f, -1.0f);
      glTexCoord2i (display->priv->redisplay_texture_width,
          display->priv->redisplay_texture_height);
      glVertex2f (1.0f, -1.0f);
      /*glTexCoord2i (display->priv->redisplay_texture_width, 0);
         glVertex2i (1, -1);
         glTexCoord2i (0, 0);
         glVertex2f (-1.0f, -1.0f);
         glTexCoord2i (0, display->priv->redisplay_texture_height);
         glVertex2f (-1.0f, 1.0f);
         glTexCoord2i (display->priv->redisplay_texture_width,
         display->priv->redisplay_texture_height);
         glVertex2f (1.0f, 1.0f); */
      glEnd ();

      glDisable (GL_TEXTURE_RECTANGLE_ARB);
    }
#endif
#if GST_GL_HAVE_GLES2
    if (USING_GLES2 (display)) {
      const GLfloat vVertices[] = { 1.0f, 1.0f, 0.0f,
        1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f,
        0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, -1.0f, 0.0f,
        1.0f, 1.0f
      };

      GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

      glClear (GL_COLOR_BUFFER_BIT);

      gst_gl_shader_use (display->priv->redisplay_shader);

      /* Load the vertex position */
      glVertexAttribPointer (display->priv->redisplay_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);

      /* Load the texture coordinate */
      glVertexAttribPointer (display->priv->redisplay_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      glEnableVertexAttribArray (display->priv->redisplay_attr_position_loc);
      glEnableVertexAttribArray (display->priv->redisplay_attr_texture_loc);

      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, display->priv->redisplay_texture);
      gst_gl_shader_set_uniform_1i (display->priv->redisplay_shader,
          "s_texture", 0);

      glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    }
#endif
  }                             /* end default opengl scene */
}


void
gst_gl_display_on_close (GstGLDisplay * display)
{
  gst_gl_display_set_error (display, "Output window was closed");
}

void
gst_gl_display_gen_texture_window_cb (GstGLDisplay * display)
{
  gst_gl_display_gen_texture_thread (display, &display->priv->gen_texture,
      display->priv->gen_texture_video_format, display->priv->gen_texture_width,
      display->priv->gen_texture_height);
}

/* Generate a texture if no one is available in the pool
 * Called in the gl thread */
void
gst_gl_display_gen_texture_thread (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  GST_TRACE ("Generating texture format:%u dimensions:%ux%u", v_format,
      width, height);

  glGenTextures (1, pTexture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
          break;
#if 0
        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (display->upload_width != display->upload_data_width ||
              display->upload_height != display->upload_data_height)
            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
          else
            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width,
                height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;
#endif
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
      }
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      break;
  }

  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  GST_LOG ("generated texture id:%d", *pTexture);
}

void
gst_gl_display_del_texture_thread (GstGLDisplay * display, GLuint * pTexture)
{
  //glDeleteTextures (1, pTexture);
}


/*------------------------------------------------------------
  ---------------------  END PRIVATE -------------------------
  ----------------------------------------------------------*/


/*------------------------------------------------------------
  --------------------- BEGIN PUBLIC -------------------------
  ----------------------------------------------------------*/

void
gst_gl_display_lock (GstGLDisplay * display)
{
  g_mutex_lock (display->mutex);
}

void
gst_gl_display_unlock (GstGLDisplay * display)
{
  g_mutex_unlock (display->mutex);
}

/* called in the gl thread */
void
gst_gl_display_check_framebuffer_status (void)
{
  GLenum status = glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT);

  switch (status) {
    case GL_FRAMEBUFFER_COMPLETE_EXT:
      break;

    case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
      GST_ERROR ("GL_FRAMEBUFFER_UNSUPPORTED");
      break;

    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
      break;

    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS");
      break;
#if GST_GL_HAVE_OPENGL
    case GL_FRAMEBUFFER_UNDEFINED:
      GST_ERROR ("GL_FRAMEBUFFER_UNDEFINED");
      break;
#endif
    default:
      GST_ERROR ("General FBO error");
  }
}

/* Called by the first gl element of a video/x-raw-gl flow */
GstGLDisplay *
gst_gl_display_new (void)
{
  return g_object_new (GST_GL_TYPE_DISPLAY, NULL);
}


/* Create an opengl context (one context for one GstGLDisplay) */
gboolean
gst_gl_display_create_context (GstGLDisplay * display,
    gulong external_gl_context)
{
  gboolean isAlive = FALSE;

  gst_gl_display_lock (display);

  if (!display->gl_window) {
    display->external_gl_context = external_gl_context;

    display->gl_thread = g_thread_create (
        (GThreadFunc) gst_gl_display_thread_create_context, display, TRUE,
        NULL);

    g_cond_wait (display->priv->cond_create_context, display->mutex);

    GST_INFO ("gl thread created");
  }

  isAlive = display->isAlive;

  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by the glimagesink element */
gboolean
gst_gl_display_redisplay (GstGLDisplay * display, GLuint texture,
    gint gl_width, gint gl_height, gint window_width, gint window_height,
    gboolean keep_aspect_ratio)
{
  gboolean isAlive;

  gst_gl_display_lock (display);
  if (display->isAlive) {

#if GST_GL_HAVE_GLES2
    if (USING_GLES2 (display)) {
      if (!display->priv->redisplay_shader) {
        gst_gl_window_send_message (display->gl_window,
            GST_GL_WINDOW_CB (gst_gl_display_thread_init_redisplay), display);
      }
    }
#endif

    if (texture) {
      display->priv->redisplay_texture = texture;
      display->priv->redisplay_texture_width = gl_width;
      display->priv->redisplay_texture_height = gl_height;
    }
    display->keep_aspect_ratio = keep_aspect_ratio;
    if (display->gl_window)
      gst_gl_window_draw (display->gl_window, window_width, window_height);
  }
  isAlive = display->isAlive;
  gst_gl_display_unlock (display);

  return isAlive;
}

void
gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data)
{
  gst_gl_display_lock (display);
  display->priv->data = data;
  display->priv->generic_callback = func;
  gst_gl_window_send_message (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_thread_run_generic), display);
  gst_gl_display_unlock (display);
}

/* Called by gst_gl_buffer_new */
void
gst_gl_display_gen_texture (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  gst_gl_display_lock (display);

  if (display->isAlive) {
    display->priv->gen_texture_width = width;
    display->priv->gen_texture_height = height;
    display->priv->gen_texture_video_format = v_format;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_gen_texture_window_cb), display);
    *pTexture = display->priv->gen_texture;
  } else
    *pTexture = 0;

  gst_gl_display_unlock (display);
}


/* Called by gst_gl_buffer_finalize */
void
gst_gl_display_del_texture (GstGLDisplay * display, GLuint * pTexture)
{
  gst_gl_display_lock (display);
  if (*pTexture) {
    gst_gl_display_del_texture_thread (display, pTexture);
  }
  gst_gl_display_unlock (display);
}

/* Called by gltestsrc and glfilter */
gboolean
gst_gl_display_gen_fbo (GstGLDisplay * display, gint width, gint height,
    GLuint * fbo, GLuint * depthbuffer)
{
  gboolean isAlive = FALSE;

  gst_gl_display_lock (display);
  if (display->isAlive) {
    display->priv->gen_fbo_width = width;
    display->priv->gen_fbo_height = height;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (display->priv->vtable.gen_fbo), display);
    *fbo = display->priv->generated_fbo;
    *depthbuffer = display->priv->generated_depth_buffer;
  }
  isAlive = display->isAlive;
  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by glfilter */
/* this function really has to be simplified...  do we really need to
   set projection this way? Wouldn't be better a set_projection
   separate call? or just make glut functions available out of
   gst-libs and call it if needed on drawcallback? -- Filippo */
/* GLCB too.. I think that only needed parameters should be
 * GstGLDisplay *display and gpointer data, or just gpointer data */
/* ..everything here has to be simplified! */
gboolean
gst_gl_display_use_fbo (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB cb, gint input_texture_width,
    gint input_texture_height, GLuint input_texture, gdouble proj_param1,
    gdouble proj_param2, gdouble proj_param3, gdouble proj_param4,
    GstGLDisplayProjection projection, gpointer * stuff)
{
  gboolean isAlive;

  gst_gl_display_lock (display);
  if (display->isAlive) {
    display->priv->use_fbo = fbo;
    display->priv->use_depth_buffer = depth_buffer;
    display->priv->use_fbo_texture = texture_fbo;
    display->priv->use_fbo_width = texture_fbo_width;
    display->priv->use_fbo_height = texture_fbo_height;
    display->priv->use_fbo_scene_cb = cb;
    display->priv->use_fbo_proj_param1 = proj_param1;
    display->priv->use_fbo_proj_param2 = proj_param2;
    display->priv->use_fbo_proj_param3 = proj_param3;
    display->priv->use_fbo_proj_param4 = proj_param4;
    display->priv->use_fbo_projection = projection;
    display->priv->use_fbo_stuff = stuff;
    display->priv->input_texture_width = input_texture_width;
    display->priv->input_texture_height = input_texture_height;
    display->priv->input_texture = input_texture;
    gst_gl_window_send_message (display->gl_window,
        display->priv->vtable.use_fbo, display);
  }
  isAlive = display->isAlive;
  gst_gl_display_unlock (display);

  return isAlive;
}

gboolean
gst_gl_display_use_fbo_v2 (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB_V2 cb, gpointer * stuff)
{
  gboolean isAlive;

  gst_gl_display_lock (display);
  if (display->isAlive) {
    display->priv->use_fbo = fbo;
    display->priv->use_depth_buffer = depth_buffer;
    display->priv->use_fbo_texture = texture_fbo;
    display->priv->use_fbo_width = texture_fbo_width;
    display->priv->use_fbo_height = texture_fbo_height;
    display->priv->use_fbo_scene_cb_v2 = cb;
    display->priv->use_fbo_stuff = stuff;
    gst_gl_window_send_message (display->gl_window,
        display->priv->vtable.use_fbo_v2, display);
  }
  isAlive = display->isAlive;
  gst_gl_display_unlock (display);

  return isAlive;
}

/* Called by gltestsrc and glfilter */
void
gst_gl_display_del_fbo (GstGLDisplay * display, GLuint fbo, GLuint depth_buffer)
{
  gst_gl_display_lock (display);
  if (display->isAlive) {
    display->priv->del_fbo = fbo;
    display->priv->del_depth_buffer = depth_buffer;
    gst_gl_window_send_message (display->gl_window,
        display->priv->vtable.del_fbo, display);
  }
  gst_gl_display_unlock (display);
}


/* Called by glfilter */
gboolean
gst_gl_display_gen_shader (GstGLDisplay * display,
    const gchar * shader_vertex_source,
    const gchar * shader_fragment_source, GstGLShader ** shader)
{
  gboolean isAlive;

  gst_gl_display_lock (display);
  if (display->isAlive) {
    display->priv->gen_shader_vertex_source = shader_vertex_source;
    display->priv->gen_shader_fragment_source = shader_fragment_source;
    gst_gl_window_send_message (display->gl_window,
        display->priv->vtable.gen_shader, display);
    if (shader)
      *shader = display->priv->gen_shader;
    display->priv->gen_shader = NULL;
    display->priv->gen_shader_vertex_source = NULL;
    display->priv->gen_shader_fragment_source = NULL;
  }
  isAlive = display->isAlive;
  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by glfilter */
void
gst_gl_display_del_shader (GstGLDisplay * display, GstGLShader * shader)
{
  gst_gl_display_lock (display);
  if (display->isAlive) {
    display->priv->del_shader = shader;
    gst_gl_window_send_message (display->gl_window,
        display->priv->vtable.del_shader, display);
  }
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_window_id (GstGLDisplay * display, guintptr window_id)
{
  gst_gl_display_lock (display);
  gst_gl_window_set_window_handle (display->gl_window, window_id);
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_reshape_callback (GstGLDisplay * display, CRCB cb)
{
  gst_gl_display_lock (display);
  display->priv->clientReshapeCallback = cb;
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_draw_callback (GstGLDisplay * display, CDCB cb)
{
  gst_gl_display_lock (display);
  display->priv->clientDrawCallback = cb;
  gst_gl_display_unlock (display);
}

void
gst_gl_display_set_client_data (GstGLDisplay * display, gpointer data)
{
  gst_gl_display_lock (display);
  display->priv->client_data = data;
  gst_gl_display_unlock (display);
}

gulong
gst_gl_display_get_internal_gl_context (GstGLDisplay * display)
{
  gulong external_gl_context = 0;
  gst_gl_display_lock (display);
  external_gl_context = gst_gl_window_get_gl_context (display->gl_window);
  gst_gl_display_unlock (display);
  return external_gl_context;
}

void
gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate)
{
  if (!activate)
    gst_gl_display_lock (display);
  gst_gl_window_activate (display->gl_window, activate);
  if (activate)
    gst_gl_display_unlock (display);
}

GstGLAPI
gst_gl_display_get_gl_api_unlocked (GstGLDisplay * display)
{
  return display->gl_api;
}

GstGLAPI
gst_gl_display_get_gl_api (GstGLDisplay * display)
{
  GstGLAPI api;

  gst_gl_display_lock (display);
  api = gst_gl_display_get_gl_api_unlocked (display);
  gst_gl_display_unlock (display);

  return api;
}

//------------------------------------------------------------
//------------------------ END PUBLIC ------------------------
//------------------------------------------------------------
