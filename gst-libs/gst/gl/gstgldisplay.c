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

#ifndef GLEW_VERSION_MAJOR
#define GLEW_VERSION_MAJOR 4
#endif

#ifndef GLEW_VERSION_MINOR
#define GLEW_VERSION_MINOR 0
#endif

/*
 * gst-launch-0.10 --gst-debug=gldisplay:N pipeline
 * N=1: errors
 * N=2: errors warnings
 * N=3: errors warnings infos
 * N=4: errors warnings infos
 * N=5: errors warnings infos logs
 */

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display");

G_DEFINE_TYPE_WITH_CODE (GstGLDisplay, gst_gl_display, G_TYPE_OBJECT,
    DEBUG_INIT);
static void gst_gl_display_finalize (GObject * object);

/* Called in the gl thread, protected by lock and unlock */
gpointer gst_gl_display_thread_create_context (GstGLDisplay * display);
void gst_gl_display_thread_destroy_context (GstGLDisplay * display);
void gst_gl_display_thread_run_generic (GstGLDisplay * display);
#ifdef OPENGL_ES2
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

//------------------------------------------------------------
//---------------------- For klass GstGLDisplay ---------------
//------------------------------------------------------------
static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;

  gst_gl_window_init_platform ();
}


static void
gst_gl_display_init (GstGLDisplay * display)
{
  //thread safe
  display->mutex = g_mutex_new ();

  //gl context
  display->gl_thread = NULL;
  display->gl_window = NULL;
  display->isAlive = TRUE;

  //conditions
  display->cond_create_context = g_cond_new ();
  display->cond_destroy_context = g_cond_new ();

  //action redisplay
  display->redisplay_texture = 0;
  display->redisplay_texture_width = 0;
  display->redisplay_texture_height = 0;
  display->keep_aspect_ratio = FALSE;
#ifdef OPENGL_ES2
  display->redisplay_shader = NULL;
  display->redisplay_attr_position_loc = 0;
  display->redisplay_attr_texture_loc = 0;
#endif

  //action gen and del texture
  display->gen_texture = 0;
  display->gen_texture_width = 0;
  display->gen_texture_height = 0;
  display->gen_texture_video_format = GST_VIDEO_FORMAT_UNKNOWN;

  //client callbacks
  display->clientReshapeCallback = NULL;
  display->clientDrawCallback = NULL;
  display->client_data = NULL;

  display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_GLSL;

  //foreign gl context
  display->external_gl_context = 0;

  //filter gen fbo
  display->gen_fbo_width = 0;
  display->gen_fbo_height = 0;
  display->generated_fbo = 0;
  display->generated_depth_buffer = 0;

  //filter use fbo
  display->use_fbo = 0;
  display->use_depth_buffer = 0;
  display->use_fbo_texture = 0;
  display->use_fbo_width = 0;
  display->use_fbo_height = 0;
  display->use_fbo_scene_cb = NULL;
  display->use_fbo_scene_cb_v2 = NULL;
  display->use_fbo_proj_param1 = 0;
  display->use_fbo_proj_param2 = 0;
  display->use_fbo_proj_param3 = 0;
  display->use_fbo_proj_param4 = 0;
  display->use_fbo_projection = 0;
  display->use_fbo_stuff = NULL;
  display->input_texture_width = 0;
  display->input_texture_height = 0;
  display->input_texture = 0;

  //filter del fbo
  display->del_fbo = 0;
  display->del_depth_buffer = 0;

  //action gen and del shader
  display->gen_shader_fragment_source = NULL;
  display->gen_shader_vertex_source = NULL;
  display->gen_shader = NULL;
  display->del_shader = NULL;

  display->uploads = NULL;
  display->downloads = NULL;

#ifdef OPENGL_ES2
  display->redisplay_vertex_shader_str =
      "attribute vec4 a_position;   \n"
      "attribute vec2 a_texCoord;   \n"
      "varying vec2 v_texCoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position; \n"
      "   v_texCoord = a_texCoord;  \n" "}                            \n";

  display->redisplay_fragment_shader_str =
      "precision mediump float;                            \n"
      "varying vec2 v_texCoord;                            \n"
      "uniform sampler2D s_texture;                        \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
      "}                                                   \n";
#endif

  display->error_message = NULL;

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

    gst_gl_window_quit_loop (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_thread_destroy_context), display);

    GST_INFO ("quit sent to gl window loop");

    g_cond_wait (display->cond_destroy_context, display->mutex);
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
  if (display->cond_destroy_context) {
    g_cond_free (display->cond_destroy_context);
    display->cond_destroy_context = NULL;
  }
  if (display->cond_create_context) {
    g_cond_free (display->cond_create_context);
    display->cond_create_context = NULL;
  }
  if (display->clientReshapeCallback)
    display->clientReshapeCallback = NULL;
  if (display->clientDrawCallback)
    display->clientDrawCallback = NULL;
  if (display->client_data)
    display->client_data = NULL;
  if (display->use_fbo_scene_cb)
    display->use_fbo_scene_cb = NULL;
  if (display->use_fbo_scene_cb_v2)
    display->use_fbo_scene_cb_v2 = NULL;
  if (display->use_fbo_stuff)
    display->use_fbo_stuff = NULL;

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

gpointer
gst_gl_display_thread_create_context (GstGLDisplay * display)
{
  GLenum err = GLEW_OK;

  gst_gl_display_lock (display);
  display->gl_window = gst_gl_window_new (display->external_gl_context);

  if (!display->gl_window) {
    gst_gl_display_set_error (display, "Failed to create gl window");
    g_cond_signal (display->cond_create_context);
    gst_gl_display_unlock (display);
    return NULL;
  }

  GST_INFO ("gl window created");

#ifndef OPENGL_ES2
  err = glewInit ();
#endif
  if (err != GLEW_OK) {
#ifndef OPENGL_ES2
    gst_gl_display_set_error (display, "Failed to init GLEW: %s",
        glewGetErrorString (err));
#endif
  } else {
#ifndef OPENGL_ES2
    //OpenGL > 1.2.0 and Glew > 1.4.0
    GString *opengl_version = NULL;
    gint opengl_version_major = 0;
    gint opengl_version_minor = 0;
#endif

    GLenum gl_err = GL_NO_ERROR;
    if (glGetString (GL_VERSION))
      GST_INFO ("GL_VERSION: %s", glGetString (GL_VERSION));

#ifndef OPENGL_ES2
    GST_INFO ("GLEW_VERSION: %s", glewGetString (GLEW_VERSION));
#endif
    if (glGetString (GL_SHADING_LANGUAGE_VERSION))
      GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
          glGetString (GL_SHADING_LANGUAGE_VERSION));
    else
      GST_INFO ("Your driver does not support GLSL (OpenGL Shading Language)");

    if (glGetString (GL_VENDOR))
      GST_INFO ("GL_VENDOR: %s", glGetString (GL_VENDOR));

    if (glGetString (GL_RENDERER))
      GST_INFO ("GL_RENDERER: %s", glGetString (GL_RENDERER));


    gl_err = glGetError ();
    if (gl_err != GL_NO_ERROR) {
      gst_gl_display_set_error (display, "glGetString error: 0x%x", gl_err);
    }
#ifndef OPENGL_ES2
    if (glGetString (GL_VERSION) && gl_err == GL_NO_ERROR) {

      opengl_version =
          g_string_truncate (g_string_new ((gchar *) glGetString (GL_VERSION)),
          3);

      sscanf (opengl_version->str, "%d.%d", &opengl_version_major,
          &opengl_version_minor);

      g_string_free (opengl_version, TRUE);

      if ((opengl_version_major < 1) ||
          (GLEW_VERSION_MAJOR < 1) ||
          (opengl_version_major < 2 && opengl_version_major >= 1
              && opengl_version_minor < 2) || (GLEW_VERSION_MAJOR < 2
              && GLEW_VERSION_MAJOR >= 1 && GLEW_VERSION_MINOR < 4)) {
        //turn off the pipeline, the old drivers are not yet supported
        gst_gl_display_set_error (display,
            "Required OpenGL >= 1.2.0 and Glew >= 1.4.0");
      }
    }
#else
    if (!GL_ES_VERSION_2_0) {
      gst_gl_display_set_error (display, "Required OpenGL ES > 2.0");
    }
#endif
  }

  //setup callbacks
  gst_gl_window_set_resize_callback (display->gl_window,
      GST_GL_WINDOW_CB2 (gst_gl_display_on_resize), display);
  gst_gl_window_set_draw_callback (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_on_draw), display);
  gst_gl_window_set_close_callback (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_on_close), display);

  g_cond_signal (display->cond_create_context);

  gst_gl_display_unlock (display);

  gst_gl_window_run_loop (display->gl_window);

  GST_INFO ("loop exited\n");

  gst_gl_display_lock (display);

  display->isAlive = FALSE;

  g_object_unref (G_OBJECT (display->gl_window));

  display->gl_window = NULL;

  g_cond_signal (display->cond_destroy_context);

  gst_gl_display_unlock (display);

  return NULL;
}


/* Called in the gl thread */
void
gst_gl_display_thread_destroy_context (GstGLDisplay * display)
{
#ifdef OPENGL_ES2
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
      display->generic_callback, display->data);

  display->generic_callback (display, display->data);
}

#ifdef OPENGL_ES2
/* Called in the gl thread */
void
gst_gl_display_thread_init_redisplay (GstGLDisplay * display)
{
  GError *error = NULL;
  display->redisplay_shader = gst_gl_shader_new ();

  gst_gl_shader_set_vertex_source (display->redisplay_shader,
      display->redisplay_vertex_shader_str);
  gst_gl_shader_set_fragment_source (display->redisplay_shader,
      display->redisplay_fragment_shader_str);

  gst_gl_shader_compile (display->redisplay_shader, &error);
  if (error) {
    gst_gl_display_set_error (display, "%s", error->message);
    g_error_free (error);
    error = NULL;
    gst_gl_shader_use (NULL);
  } else {
    display->redisplay_attr_position_loc =
        gst_gl_shader_get_attribute_location (display->redisplay_shader,
        "a_position");
    display->redisplay_attr_texture_loc =
        gst_gl_shader_get_attribute_location (display->redisplay_shader,
        "a_texCoord");
  }
}
#endif

/* Called in the gl thread */
void
gst_gl_display_thread_gen_fbo (GstGLDisplay * display)
{
  //a texture must be attached to the FBO
  GLuint fake_texture = 0;

  GST_TRACE ("creating FBO dimensions:%ux%u", display->gen_fbo_width,
      display->gen_fbo_height);

  //-- generate frame buffer object

  if (!GLEW_EXT_framebuffer_object) {
    //turn off the pipeline because Frame buffer object is a not present
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object not supported");
    return;
  }
  //setup FBO
  glGenFramebuffersEXT (1, &display->generated_fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->generated_fbo);

  //setup the render buffer for depth
  glGenRenderbuffersEXT (1, &display->generated_depth_buffer);
  glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, display->generated_depth_buffer);
#ifndef OPENGL_ES2
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
      display->gen_fbo_width, display->gen_fbo_height);
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT,
      display->gen_fbo_width, display->gen_fbo_height);
#else
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16,
      display->gen_fbo_width, display->gen_fbo_height);
#endif

  //setup a texture to render to
  glGenTextures (1, &fake_texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
      display->gen_fbo_width, display->gen_fbo_height, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  //attach the texture to the FBO to renderer to
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

  //attach the depth render buffer to the FBO
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, display->generated_depth_buffer);

#ifndef OPENGL_ES2
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, display->generated_depth_buffer);
#endif

  if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) !=
      GL_FRAMEBUFFER_COMPLETE_EXT)
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");

  //unbind the FBO
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  glDeleteTextures (1, &fake_texture);
}


/* Called in the gl thread */
void
gst_gl_display_thread_use_fbo (GstGLDisplay * display)
{
#ifdef OPENGL_ES2
  GLint viewport_dim[4];
#endif

  GST_TRACE ("Binding v1 FBO %u dimensions:%ux%u with texture:%u "
      "dimensions:%ux%u", display->use_fbo, display->use_fbo_width,
      display->use_fbo_height, display->use_fbo_texture,
      display->input_texture_width, display->input_texture_height);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->use_fbo);

  //setup a texture to render to
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture);

  //attach the texture to the FBO to renderer to
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture, 0);

  if (GLEW_ARB_fragment_shader)
    gst_gl_shader_use (NULL);

#ifndef OPENGL_ES2
  glPushAttrib (GL_VIEWPORT_BIT);
  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();

  switch (display->use_fbo_projection) {
    case GST_GL_DISPLAY_PROJECTION_ORTHO2D:
      gluOrtho2D (display->use_fbo_proj_param1, display->use_fbo_proj_param2,
          display->use_fbo_proj_param3, display->use_fbo_proj_param4);
      break;
    case GST_GL_DISPLAY_PROJECTION_PERSPECTIVE:
      gluPerspective (display->use_fbo_proj_param1,
          display->use_fbo_proj_param2, display->use_fbo_proj_param3,
          display->use_fbo_proj_param4);
      break;
    default:
      gst_gl_display_set_error (display, "Unknow fbo projection %d",
          display->use_fbo_projection);
  }

  glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();
#else // OPENGL_ES2
  glGetIntegerv (GL_VIEWPORT, viewport_dim);
#endif

  glViewport (0, 0, display->use_fbo_width, display->use_fbo_height);

#ifndef OPENGL_ES2
  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);
#endif

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  //the opengl scene
  display->use_fbo_scene_cb (display->input_texture_width,
      display->input_texture_height, display->input_texture,
      display->use_fbo_stuff);

#ifndef OPENGL_ES2
  glDrawBuffer (GL_NONE);
  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);
  glPopMatrix ();
  glPopAttrib ();
#else
  glViewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);
#endif

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}


/* Called in a gl thread
 * Need full shader support */
void
gst_gl_display_thread_use_fbo_v2 (GstGLDisplay * display)
{
  GLint viewport_dim[4];

  GST_TRACE ("Binding v2 FBO %u dimensions:%ux%u with texture:%u ",
      display->use_fbo, display->use_fbo_width,
      display->use_fbo_height, display->use_fbo_texture);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->use_fbo);

  //setup a texture to render to
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture);

  //attach the texture to the FBO to renderer to
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture, 0);

  glGetIntegerv (GL_VIEWPORT, viewport_dim);

  glViewport (0, 0, display->use_fbo_width, display->use_fbo_height);

#ifndef OPENGL_ES2
  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);
#endif

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  //the opengl scene
  display->use_fbo_scene_cb_v2 (display->use_fbo_stuff);

#ifndef OPENGL_ES2
  glDrawBuffer (GL_NONE);
#endif

  glViewport (viewport_dim[0], viewport_dim[1],
      viewport_dim[2], viewport_dim[3]);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}


/* Called in the gl thread */
void
gst_gl_display_thread_del_fbo (GstGLDisplay * display)
{
  GST_TRACE ("Deleting FBO %u", display->del_fbo);

  if (display->del_fbo) {
    glDeleteFramebuffersEXT (1, &display->del_fbo);
    display->del_fbo = 0;
  }
  if (display->del_depth_buffer) {
    glDeleteRenderbuffersEXT (1, &display->del_depth_buffer);
    display->del_depth_buffer = 0;
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_gen_shader (GstGLDisplay * display)
{
  GST_TRACE ("Generating shader %" GST_PTR_FORMAT, display->gen_shader);

  if (GLEW_ARB_fragment_shader) {
    if (display->gen_shader_vertex_source ||
        display->gen_shader_fragment_source) {
      GError *error = NULL;

      display->gen_shader = gst_gl_shader_new ();

      if (display->gen_shader_vertex_source)
        gst_gl_shader_set_vertex_source (display->gen_shader,
            display->gen_shader_vertex_source);

      if (display->gen_shader_fragment_source)
        gst_gl_shader_set_fragment_source (display->gen_shader,
            display->gen_shader_fragment_source);

      gst_gl_shader_compile (display->gen_shader, &error);
      if (error) {
        gst_gl_display_set_error (display, "%s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_shader_use (NULL);
        g_object_unref (G_OBJECT (display->gen_shader));
        display->gen_shader = NULL;
      }
    }
  } else {
    gst_gl_display_set_error (display,
        "One of the filter required ARB_fragment_shader");
    display->gen_shader = NULL;
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_del_shader (GstGLDisplay * display)
{
  GST_TRACE ("Deleting shader %" GST_PTR_FORMAT, display->del_shader);

  if (display->del_shader) {
    g_object_unref (G_OBJECT (display->del_shader));
    display->del_shader = NULL;
  }
}


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
  if (display->clientReshapeCallback)
    display->clientReshapeCallback (width, height, display->client_data);

  //default reshape
  else {
    if (display->keep_aspect_ratio) {
      GstVideoRectangle src, dst, result;

      src.x = 0;
      src.y = 0;
      src.w = display->redisplay_texture_width;
      src.h = display->redisplay_texture_height;

      dst.x = 0;
      dst.y = 0;
      dst.w = width;
      dst.h = height;

      gst_video_sink_center_rect (src, dst, &result, TRUE);
      glViewport (result.x, result.y, result.w, result.h);
    } else {
      glViewport (0, 0, width, height);
    }
#ifndef OPENGL_ES2
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
    gluOrtho2D (0, width, 0, height);
    glMatrixMode (GL_MODELVIEW);
#endif
  }
}


void
gst_gl_display_on_draw (GstGLDisplay * display)
{
  //check if tecture is ready for being drawn
  if (!display->redisplay_texture)
    return;

  //opengl scene
  GST_TRACE ("on draw");

  //make sure that the environnement is clean
  if (display->colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
    glUseProgramObjectARB (0);

#ifndef OPENGL_ES2
  glDisable (GL_TEXTURE_RECTANGLE_ARB);
#endif

  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  //check if a client draw callback is registered
  if (display->clientDrawCallback) {
    gboolean doRedisplay =
        display->clientDrawCallback (display->redisplay_texture,
        display->redisplay_texture_width, display->redisplay_texture_height,
        display->client_data);

    if (doRedisplay && display->gl_window)
      gst_gl_window_draw_unlocked (display->gl_window,
          display->redisplay_texture_width, display->redisplay_texture_height);
  }
  //default opengl scene
  else {

#ifndef OPENGL_ES2
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->redisplay_texture);
    glEnable (GL_TEXTURE_RECTANGLE_ARB);

    glBegin (GL_QUADS);
    /* gst images are top-down while opengl plane is bottom-up */
    glTexCoord2i (display->redisplay_texture_width, 0);
    glVertex2f (1.0f, 1.0f);
    glTexCoord2i (0, 0);
    glVertex2f (-1.0f, 1.0f);
    glTexCoord2i (0, display->redisplay_texture_height);
    glVertex2f (-1.0f, -1.0f);
    glTexCoord2i (display->redisplay_texture_width,
        display->redisplay_texture_height);
    glVertex2f (1.0f, -1.0f);
    /*glTexCoord2i (display->redisplay_texture_width, 0);
       glVertex2i (1, -1);
       glTexCoord2i (0, 0);
       glVertex2f (-1.0f, -1.0f);
       glTexCoord2i (0, display->redisplay_texture_height);
       glVertex2f (-1.0f, 1.0f);
       glTexCoord2i (display->redisplay_texture_width,
       display->redisplay_texture_height);
       glVertex2f (1.0f, 1.0f); */
    glEnd ();

    glDisable (GL_TEXTURE_RECTANGLE_ARB);

#else //OPENGL_ES2

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

    gst_gl_shader_use (display->redisplay_shader);

    //Load the vertex position
    glVertexAttribPointer (display->redisplay_attr_position_loc, 3, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), vVertices);

    //Load the texture coordinate
    glVertexAttribPointer (display->redisplay_attr_texture_loc, 2, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

    glEnableVertexAttribArray (display->redisplay_attr_position_loc);
    glEnableVertexAttribArray (display->redisplay_attr_texture_loc);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, display->redisplay_texture);
    gst_gl_shader_set_uniform_1i (display->redisplay_shader, "s_texture", 0);

    glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
#endif

  }                             //end default opengl scene
}


void
gst_gl_display_on_close (GstGLDisplay * display)
{
  gst_gl_display_set_error (display, "Output window was closed");
}

void
gst_gl_display_gen_texture_window_cb (GstGLDisplay * display)
{
  gst_gl_display_gen_texture_thread (display, &display->gen_texture,
      display->gen_texture_video_format, display->gen_texture_width,
      display->gen_texture_height);
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
/*        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (display->upload_width != display->upload_data_width ||
              display->upload_height != display->upload_data_height)
            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
          else
            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width,
                height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;*/
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
//      gst_gl_display_set_error (display, "Unsupported upload video format %d",
//          display->upload_video_format);
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


//------------------------------------------------------------
//---------------------  END PRIVATE -------------------------
//------------------------------------------------------------


//------------------------------------------------------------
//---------------------- BEGIN PUBLIC ------------------------
//------------------------------------------------------------

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
#ifndef OPENGL_ES2
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
  return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
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

    g_cond_wait (display->cond_create_context, display->mutex);

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
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive) {

#ifdef OPENGL_ES2
    if (!display->redisplay_shader) {
      gst_gl_window_send_message (display->gl_window,
          GST_GL_WINDOW_CB (gst_gl_display_thread_init_redisplay), display);
    }
#endif

    if (texture) {
      display->redisplay_texture = texture;
      display->redisplay_texture_width = gl_width;
      display->redisplay_texture_height = gl_height;
    }
    display->keep_aspect_ratio = keep_aspect_ratio;
    if (display->gl_window)
      gst_gl_window_draw (display->gl_window, window_width, window_height);
    isAlive = display->isAlive;
  }
  gst_gl_display_unlock (display);

  return isAlive;
}

void
gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data)
{
  gst_gl_display_lock (display);
  display->data = data;
  display->generic_callback = func;
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
    display->gen_texture_width = width;
    display->gen_texture_height = height;
    display->gen_texture_video_format = v_format;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_gen_texture_window_cb), display);
    *pTexture = display->gen_texture;
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
    display->gen_fbo_width = width;
    display->gen_fbo_height = height;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_thread_gen_fbo), display);
    *fbo = display->generated_fbo;
    *depthbuffer = display->generated_depth_buffer;
    isAlive = display->isAlive;
  }
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
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive) {
    display->use_fbo = fbo;
    display->use_depth_buffer = depth_buffer;
    display->use_fbo_texture = texture_fbo;
    display->use_fbo_width = texture_fbo_width;
    display->use_fbo_height = texture_fbo_height;
    display->use_fbo_scene_cb = cb;
    display->use_fbo_proj_param1 = proj_param1;
    display->use_fbo_proj_param2 = proj_param2;
    display->use_fbo_proj_param3 = proj_param3;
    display->use_fbo_proj_param4 = proj_param4;
    display->use_fbo_projection = projection;
    display->use_fbo_stuff = stuff;
    display->input_texture_width = input_texture_width;
    display->input_texture_height = input_texture_height;
    display->input_texture = input_texture;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_thread_use_fbo), display);
    isAlive = display->isAlive;
  }
  gst_gl_display_unlock (display);

  return isAlive;
}

gboolean
gst_gl_display_use_fbo_v2 (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB_V2 cb, gpointer * stuff)
{
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive) {
    display->use_fbo = fbo;
    display->use_depth_buffer = depth_buffer;
    display->use_fbo_texture = texture_fbo;
    display->use_fbo_width = texture_fbo_width;
    display->use_fbo_height = texture_fbo_height;
    display->use_fbo_scene_cb_v2 = cb;
    display->use_fbo_stuff = stuff;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_thread_use_fbo_v2), display);
    isAlive = display->isAlive;
  }
  gst_gl_display_unlock (display);

  return isAlive;
}

/* Called by gltestsrc and glfilter */
void
gst_gl_display_del_fbo (GstGLDisplay * display, GLuint fbo, GLuint depth_buffer)
{
  gst_gl_display_lock (display);
  display->del_fbo = fbo;
  display->del_depth_buffer = depth_buffer;
  gst_gl_window_send_message (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_thread_del_fbo), display);
  gst_gl_display_unlock (display);
}


/* Called by glfilter */
gboolean
gst_gl_display_gen_shader (GstGLDisplay * display,
    const gchar * shader_vertex_source,
    const gchar * shader_fragment_source, GstGLShader ** shader)
{
  gboolean isAlive = FALSE;

  gst_gl_display_lock (display);
  display->gen_shader_vertex_source = shader_vertex_source;
  display->gen_shader_fragment_source = shader_fragment_source;
  gst_gl_window_send_message (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_thread_gen_shader), display);
  isAlive = display->isAlive;
  if (shader)
    *shader = display->gen_shader;
  display->gen_shader = NULL;
  display->gen_shader_vertex_source = NULL;
  display->gen_shader_fragment_source = NULL;
  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by glfilter */
void
gst_gl_display_del_shader (GstGLDisplay * display, GstGLShader * shader)
{
  gst_gl_display_lock (display);
  display->del_shader = shader;
  gst_gl_window_send_message (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_thread_del_shader), display);
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_window_id (GstGLDisplay * display, gulong window_id)
{
  gst_gl_display_lock (display);
  gst_gl_window_set_external_window_id (display->gl_window, window_id);
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_reshape_callback (GstGLDisplay * display, CRCB cb)
{
  gst_gl_display_lock (display);
  display->clientReshapeCallback = cb;
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_draw_callback (GstGLDisplay * display, CDCB cb)
{
  gst_gl_display_lock (display);
  display->clientDrawCallback = cb;
  gst_gl_display_unlock (display);
}

void
gst_gl_display_set_client_data (GstGLDisplay * display, gpointer data)
{
  gst_gl_display_lock (display);
  display->client_data = data;
  gst_gl_display_unlock (display);
}

gulong
gst_gl_display_get_internal_gl_context (GstGLDisplay * display)
{
  gulong external_gl_context = 0;
  gst_gl_display_lock (display);
  external_gl_context =
      gst_gl_window_get_internal_gl_context (display->gl_window);
  gst_gl_display_unlock (display);
  return external_gl_context;
}

void
gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate)
{
  if (!activate)
    gst_gl_display_lock (display);
  gst_gl_window_activate_gl_context (display->gl_window, activate);
  if (activate)
    gst_gl_display_unlock (display);
}

//------------------------------------------------------------
//------------------------ END PUBLIC ------------------------
//------------------------------------------------------------
