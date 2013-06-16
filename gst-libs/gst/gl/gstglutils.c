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

#include <stdio.h>

#include <gst/gst.h>

#include "gstglutils.h"
#include "gstglfeature.h"
#include "gstglframebuffer.h"

#ifndef GL_FRAMEBUFFER_UNDEFINED
#define GL_FRAMEBUFFER_UNDEFINED          0x8219
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#endif
#ifndef GL_FRAMEBUFFER_UNSUPPORTED
#define GL_FRAMEBUFFER_UNSUPPORTED        0x8CDD
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS 0x8CD9
#endif

#define USING_OPENGL(display) (display->gl_api & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (display->gl_api & GST_GL_API_OPENGL3)
#define USING_GLES(display) (display->gl_api & GST_GL_API_GLES)
#define USING_GLES2(display) (display->gl_api & GST_GL_API_GLES2)
#define USING_GLES3(display) (display->gl_api & GST_GL_API_GLES3)

static GLuint gen_texture;
static GLuint gen_texture_width;
static GLuint gen_texture_height;
static GstVideoFormat gen_texture_video_format;

static GLuint *del_texture;

/* action gen and del shader */
static const gchar *gen_shader_fragment_source;
static const gchar *gen_shader_vertex_source;
static GstGLShader *gen_shader;
static GstGLShader *del_shader;

static void _gen_shader (GstGLDisplay * display);
static void _del_shader (GstGLDisplay * display);

static gchar *error_message;

static void
gst_gl_display_gen_texture_window_cb (GstGLDisplay * display)
{
  gst_gl_display_gen_texture_thread (display, &gen_texture,
      gen_texture_video_format, gen_texture_width, gen_texture_height);
}

/* Generate a texture if no one is available in the pool
 * Called in the gl thread */
void
gst_gl_display_gen_texture_thread (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  const GstGLFuncs *gl = display->gl_vtable;

  GST_TRACE ("Generating texture format:%u dimensions:%ux%u", v_format,
      width, height);

  gl->GenTextures (1, pTexture);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture);

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
    {
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    {
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
#endif
          gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (display->upload_width != display->upload_data_width ||
              display->upload_height != display->upload_data_height)
            gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
          else
            gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width,
                height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
      }
#endif
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
    {
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    }
    default:
    {
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      break;
    }
  }

  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
      GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
      GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  GST_LOG ("generated texture id:%d", *pTexture);
}

void
gst_gl_display_del_texture_window_cb (GstGLDisplay * display)
{
  glDeleteTextures (1, del_texture);
}

/* called in the gl thread */
gboolean
gst_gl_display_check_framebuffer_status (GstGLDisplay * display)
{
  GLenum status = 0;
  status = display->gl_vtable->CheckFramebufferStatus (GL_FRAMEBUFFER);

  switch (status) {
    case GL_FRAMEBUFFER_COMPLETE:
      return TRUE;
      break;

    case GL_FRAMEBUFFER_UNSUPPORTED:
      GST_ERROR ("GL_FRAMEBUFFER_UNSUPPORTED");
      break;

    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
      break;

    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
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

  return FALSE;
}

void
gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate)
{
  GstGLWindow *window;

  g_return_if_fail (GST_IS_GL_DISPLAY (display));

  if (!activate)
    gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);

  gst_gl_window_activate (window, activate);

  if (activate)
    gst_gl_display_unlock (display);

  gst_object_unref (window);
}

void
gst_gl_display_gen_texture (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  GstGLWindow *window;

  gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);

  if (gst_gl_window_is_running (window)) {
    gen_texture_width = width;
    gen_texture_height = height;
    gen_texture_video_format = v_format;
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (gst_gl_display_gen_texture_window_cb), display);
    *pTexture = gen_texture;
  } else
    *pTexture = 0;

  gst_object_unref (window);

  gst_gl_display_unlock (display);
}

void
gst_gl_display_del_texture (GstGLDisplay * display, GLuint * pTexture)
{
  GstGLWindow *window;

  gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);
  if (gst_gl_window_is_running (window) && *pTexture) {
    del_texture = pTexture;
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (gst_gl_display_del_texture_window_cb), display);
  }

  gst_object_unref (window);

  gst_gl_display_unlock (display);
}

typedef struct _GenFBO
{
  GstGLFramebuffer *frame;
  gint width, height;
  GLuint *fbo, *depth;
} GenFBO;

static void
_gen_fbo (GstGLDisplay * display, GenFBO * data)
{
  gst_gl_framebuffer_generate (data->frame, data->width, data->height,
      data->fbo, data->depth);
}

gboolean
gst_gl_display_gen_fbo (GstGLDisplay * display, gint width, gint height,
    GLuint * fbo, GLuint * depthbuffer)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  GenFBO data = { frame, width, height, fbo, depthbuffer };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _gen_fbo, &data);

  gst_object_unref (frame);

  return TRUE;
}

typedef struct _UseFBO
{
  GstGLFramebuffer *frame;
  gint texture_fbo_width;
  gint texture_fbo_height;
  GLuint fbo;
  GLuint depth_buffer;
  GLuint texture_fbo;
  GLCB cb;
  gint input_tex_width;
  gint input_tex_height;
  GLuint input_tex;
  gdouble proj_param1;
  gdouble proj_param2;
  gdouble proj_param3;
  gdouble proj_param4;
  GstGLDisplayProjection projection;
  gpointer stuff;
} UseFBO;

static void
_use_fbo (GstGLDisplay * display, UseFBO * data)
{
  gst_gl_framebuffer_use (data->frame, data->texture_fbo_width,
      data->texture_fbo_height, data->fbo, data->depth_buffer,
      data->texture_fbo, data->cb, data->input_tex_width,
      data->input_tex_height, data->input_tex, data->proj_param1,
      data->proj_param2, data->proj_param3, data->proj_param4, data->projection,
      data->stuff);
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
    GLuint texture_fbo, GLCB cb, gint input_tex_width,
    gint input_tex_height, GLuint input_tex, gdouble proj_param1,
    gdouble proj_param2, gdouble proj_param3, gdouble proj_param4,
    GstGLDisplayProjection projection, gpointer stuff)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  UseFBO data =
      { frame, texture_fbo_width, texture_fbo_height, fbo, depth_buffer,
    texture_fbo, cb, input_tex_width, input_tex_height, input_tex,
    proj_param1, proj_param2, proj_param3, proj_param4, projection, stuff
  };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _use_fbo, &data);

  gst_object_unref (frame);

  return TRUE;
}

typedef struct _UseFBO2
{
  GstGLFramebuffer *frame;
  gint texture_fbo_width;
  gint texture_fbo_height;
  GLuint fbo;
  GLuint depth_buffer;
  GLuint texture_fbo;
  GLCB_V2 cb;
  gpointer stuff;
} UseFBO2;

static void
_use_fbo_v2 (GstGLDisplay * display, UseFBO2 * data)
{
  gst_gl_framebuffer_use_v2 (data->frame, data->texture_fbo_width,
      data->texture_fbo_height, data->fbo, data->depth_buffer,
      data->texture_fbo, data->cb, data->stuff);
}

gboolean
gst_gl_display_use_fbo_v2 (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB_V2 cb, gpointer stuff)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  UseFBO2 data =
      { frame, texture_fbo_width, texture_fbo_height, fbo, depth_buffer,
    texture_fbo, cb, stuff
  };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _use_fbo_v2,
      &data);

  gst_object_unref (frame);

  return TRUE;
}

typedef struct _DelFBO
{
  GstGLFramebuffer *frame;
  GLuint fbo;
  GLuint depth;
} DelFBO;

/* Called in the gl thread */
static void
_del_fbo (GstGLDisplay * display, DelFBO * data)
{
  gst_gl_framebuffer_delete (data->frame, data->fbo, data->depth);
}

/* Called by gltestsrc and glfilter */
void
gst_gl_display_del_fbo (GstGLDisplay * display, GLuint fbo, GLuint depth_buffer)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  DelFBO data = { frame, fbo, depth_buffer };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _del_fbo, &data);

  gst_object_unref (frame);
}


/* Called by glfilter */
gboolean
gst_gl_display_gen_shader (GstGLDisplay * display,
    const gchar * shader_vertex_source,
    const gchar * shader_fragment_source, GstGLShader ** shader)
{
  gboolean alive;
  GstGLWindow *window;

  gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);
  if (gst_gl_window_is_running (window)) {
    gen_shader_vertex_source = shader_vertex_source;
    gen_shader_fragment_source = shader_fragment_source;
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (_gen_shader), display);
    if (shader)
      *shader = gen_shader;
    gen_shader = NULL;
    gen_shader_vertex_source = NULL;
    gen_shader_fragment_source = NULL;
  }
  alive = gst_gl_window_is_running (window);

  gst_object_unref (window);
  gst_gl_display_unlock (display);

  return alive;
}

void
gst_gl_display_set_error (GstGLDisplay * display, const char *format, ...)
{
  va_list args;

  if (error_message)
    g_free (error_message);

  va_start (args, format);
  error_message = g_strdup_vprintf (format, args);
  va_end (args);

  GST_WARNING ("%s", error_message);
}

gchar *
gst_gl_display_get_error (void)
{
  return error_message;
}

/* Called by glfilter */
void
gst_gl_display_del_shader (GstGLDisplay * display, GstGLShader * shader)
{
  GstGLWindow *window;

  gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);
  if (gst_gl_window_is_running (window)) {
    del_shader = shader;
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (_del_shader), display);
  }

  gst_object_unref (window);
  gst_gl_display_unlock (display);
}

/* Called in the gl thread */
static void
_gen_shader (GstGLDisplay * display)
{
  const GstGLFuncs *gl = display->gl_vtable;

  GST_TRACE ("Generating shader %" GST_PTR_FORMAT, gen_shader);

  if (gl->CreateProgramObject || gl->CreateProgram) {
    if (gen_shader_vertex_source || gen_shader_fragment_source) {
      GError *error = NULL;

      gen_shader = gst_gl_shader_new (display);

      if (gen_shader_vertex_source)
        gst_gl_shader_set_vertex_source (gen_shader, gen_shader_vertex_source);

      if (gen_shader_fragment_source)
        gst_gl_shader_set_fragment_source (gen_shader,
            gen_shader_fragment_source);

      gst_gl_shader_compile (gen_shader, &error);
      if (error) {
        gst_gl_display_set_error (display, "%s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_display_clear_shader (display);
        gst_object_unref (gen_shader);
        gen_shader = NULL;
      }
    }
  } else {
    gst_gl_display_set_error (display,
        "One of the filter required ARB_fragment_shader");
    gen_shader = NULL;
  }
}

/* Called in the gl thread */
static void
_del_shader (GstGLDisplay * display)
{
  GST_TRACE ("Deleting shader %" GST_PTR_FORMAT, del_shader);

  if (del_shader) {
    gst_object_unref (del_shader);
    del_shader = NULL;
  }
}
