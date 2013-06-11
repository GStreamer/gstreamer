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

/* filter gen fbo */
static GLuint gen_fbo_width;
static GLuint gen_fbo_height;
static GLuint generated_fbo;
static GLuint generated_depth_buffer;

/* filter use fbo */
static GLuint use_fbo;
static GLuint use_depth_buffer;
static GLuint use_fbo_texture;
static GLuint use_fbo_width;
static GLuint use_fbo_height;
static GLCB use_fbo_scene_cb;
static GLCB_V2 use_fbo_scene_cb_v2;
static gdouble use_fbo_proj_param1;
static gdouble use_fbo_proj_param2;
static gdouble use_fbo_proj_param3;
static gdouble use_fbo_proj_param4;
static GstGLDisplayProjection use_fbo_projection;
static gpointer *use_fbo_stuff;
static GLuint input_texture_width;
static GLuint input_texture_height;
static GLuint input_texture;

/* filter del fbo */
static GLuint del_fbo;
static GLuint del_depth_buffer;

/* action gen and del shader */
static const gchar *gen_shader_fragment_source;
static const gchar *gen_shader_vertex_source;
static GstGLShader *gen_shader;
static GstGLShader *del_shader;

static void _gen_fbo (GstGLDisplay * display);
static void _use_fbo (GstGLDisplay * display);
static void _use_fbo_v2 (GstGLDisplay * display);
static void _del_fbo (GstGLDisplay * display);
static void _gen_shader (GstGLDisplay * display);
static void _del_shader (GstGLDisplay * display);

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
gst_gl_display_del_texture_thread (GstGLDisplay * display, GLuint * pTexture)
{
  //glDeleteTextures (1, pTexture);
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
gst_gl_display_gen_texture (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  gst_gl_display_lock (display);

  if (display->isAlive) {
    gen_texture_width = width;
    gen_texture_height = height;
    gen_texture_video_format = v_format;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (gst_gl_display_gen_texture_window_cb), display);
    *pTexture = gen_texture;
  } else
    *pTexture = 0;

  gst_gl_display_unlock (display);
}

void
gst_gl_display_del_texture (GstGLDisplay * display, GLuint * pTexture)
{
  gst_gl_display_lock (display);
  if (*pTexture) {
    gst_gl_display_del_texture_thread (display, pTexture);
  }
  gst_gl_display_unlock (display);
}

gboolean
gst_gl_display_gen_fbo (GstGLDisplay * display, gint width, gint height,
    GLuint * fbo, GLuint * depthbuffer)
{
  gboolean isAlive = FALSE;

  gst_gl_display_lock (display);
  if (display->isAlive) {
    gen_fbo_width = width;
    gen_fbo_height = height;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (_gen_fbo),
        display);
    *fbo = generated_fbo;
    *depthbuffer = generated_depth_buffer;
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
    GLuint texture_fbo, GLCB cb, gint input_tex_width,
    gint input_tex_height, GLuint input_tex, gdouble proj_param1,
    gdouble proj_param2, gdouble proj_param3, gdouble proj_param4,
    GstGLDisplayProjection projection, gpointer * stuff)
{
  gboolean isAlive;

  gst_gl_display_lock (display);
  if (display->isAlive) {
    use_fbo = fbo;
    use_depth_buffer = depth_buffer;
    use_fbo_texture = texture_fbo;
    use_fbo_width = texture_fbo_width;
    use_fbo_height = texture_fbo_height;
    use_fbo_scene_cb = cb;
    use_fbo_proj_param1 = proj_param1;
    use_fbo_proj_param2 = proj_param2;
    use_fbo_proj_param3 = proj_param3;
    use_fbo_proj_param4 = proj_param4;
    use_fbo_projection = projection;
    use_fbo_stuff = stuff;
    input_texture_width = input_tex_width;
    input_texture_height = input_tex_height;
    input_texture = input_tex;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (_use_fbo),
        display);
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
    use_fbo = fbo;
    use_depth_buffer = depth_buffer;
    use_fbo_texture = texture_fbo;
    use_fbo_width = texture_fbo_width;
    use_fbo_height = texture_fbo_height;
    use_fbo_scene_cb_v2 = cb;
    use_fbo_stuff = stuff;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (_use_fbo_v2), display);
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
    del_fbo = fbo;
    del_depth_buffer = depth_buffer;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (_del_fbo),
        display);
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
    gen_shader_vertex_source = shader_vertex_source;
    gen_shader_fragment_source = shader_fragment_source;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (_gen_shader), display);
    if (shader)
      *shader = gen_shader;
    gen_shader = NULL;
    gen_shader_vertex_source = NULL;
    gen_shader_fragment_source = NULL;
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
    del_shader = shader;
    gst_gl_window_send_message (display->gl_window,
        GST_GL_WINDOW_CB (_del_shader), display);
  }
  gst_gl_display_unlock (display);
}

void
_gen_fbo (GstGLDisplay * display)
{
  /* a texture must be attached to the FBO */
  const GstGLFuncs *gl = display->gl_vtable;
  GLuint fake_texture = 0;

  GST_TRACE ("creating FBO dimensions:%ux%u", gen_fbo_width, gen_fbo_height);

  /* -- generate frame buffer object */

  if (!gl->GenFramebuffers) {
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object not supported");
    return;
  }
  /* setup FBO */
  gl->GenFramebuffers (1, &generated_fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, generated_fbo);

  /* setup the render buffer for depth */
  gl->GenRenderbuffers (1, &generated_depth_buffer);
  gl->BindRenderbuffer (GL_RENDERBUFFER, generated_depth_buffer);

  if (USING_OPENGL (display)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
        gen_fbo_width, gen_fbo_height);
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
        gen_fbo_width, gen_fbo_height);
  }
  if (USING_GLES2 (display)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
        gen_fbo_width, gen_fbo_height);
  }

  /* setup a texture to render to */
  gl->GenTextures (1, &fake_texture);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
      gen_fbo_width, gen_fbo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
      GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
      GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_RENDERBUFFER, generated_depth_buffer);

  if (USING_OPENGL (display)) {
    gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, generated_depth_buffer);
  }

  if (gl->CheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteTextures (1, &fake_texture);
}

static void
_use_fbo (GstGLDisplay * display)
{
  const GstGLFuncs *gl = display->gl_vtable;
#if GST_GL_HAVE_GLES2
  GLint viewport_dim[4];
#endif

  GST_TRACE ("Binding v1 FBO %u dimensions:%ux%u with texture:%u "
      "dimensions:%ux%u", use_fbo, use_fbo_width,
      use_fbo_height, use_fbo_texture,
      input_texture_width, input_texture_height);

  gl->BindFramebuffer (GL_FRAMEBUFFER, use_fbo);

  /*setup a texture to render to */
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, use_fbo_texture);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_RECTANGLE_ARB, use_fbo_texture, 0);

  gst_gl_display_clear_shader (display);


#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (display)) {
    gl->PushAttrib (GL_VIEWPORT_BIT);
    gl->MatrixMode (GL_PROJECTION);
    gl->PushMatrix ();
    gl->LoadIdentity ();

    switch (use_fbo_projection) {
      case GST_GL_DISPLAY_PROJECTION_ORTHO2D:
        gluOrtho2D (use_fbo_proj_param1,
            use_fbo_proj_param2, use_fbo_proj_param3, use_fbo_proj_param4);
        break;
      case GST_GL_DISPLAY_PROJECTION_PERSPECTIVE:
        gluPerspective (use_fbo_proj_param1,
            use_fbo_proj_param2, use_fbo_proj_param3, use_fbo_proj_param4);
        break;
      default:
        gst_gl_display_set_error (display, "Unknow fbo projection %d",
            use_fbo_projection);
    }

    gl->MatrixMode (GL_MODELVIEW);
    gl->PushMatrix ();
    gl->LoadIdentity ();
  }
#endif
#if GST_GL_HAVE_GLES2
  if (USING_GLES2 (display))
    gl->GetIntegerv (GL_VIEWPORT, viewport_dim);
#endif

  gl->Viewport (0, 0, use_fbo_width, use_fbo_height);

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (display)) {
    const GLenum rt[] = { GL_COLOR_ATTACHMENT0 };
    gl->DrawBuffers (1, rt);
  }
#endif

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  use_fbo_scene_cb (input_texture_width,
      input_texture_height, input_texture, use_fbo_stuff);

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (display)) {
    const GLenum rt[] = { GL_NONE };
    gl->DrawBuffers (1, rt);
    gl->MatrixMode (GL_PROJECTION);
    gl->PopMatrix ();
    gl->MatrixMode (GL_MODELVIEW);
    gl->PopMatrix ();
    gl->PopAttrib ();
  }
#endif
#if GST_GL_HAVE_GLES2
  if (USING_GLES2 (display)) {
    gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
        viewport_dim[3]);
  }
#endif

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
}

/* Called in a gl thread
 * Need full shader support */
static void
_use_fbo_v2 (GstGLDisplay * display)
{
  const GstGLFuncs *gl = display->gl_vtable;
  GLint viewport_dim[4];

  GST_TRACE ("Binding v2 FBO %u dimensions:%ux%u with texture:%u ",
      use_fbo, use_fbo_width, use_fbo_height, use_fbo_texture);

  gl->BindFramebuffer (GL_FRAMEBUFFER, use_fbo);

  /* setup a texture to render to */
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, use_fbo_texture);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_RECTANGLE_ARB, use_fbo_texture, 0);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, use_fbo_width, use_fbo_height);

  gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* the opengl scene */
  use_fbo_scene_cb_v2 (use_fbo_stuff);

  gl->DrawBuffer (GL_NONE);

  gl->Viewport (viewport_dim[0], viewport_dim[1],
      viewport_dim[2], viewport_dim[3]);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
}

/* Called in the gl thread */
static void
_del_fbo (GstGLDisplay * display)
{
  const GstGLFuncs *gl = display->gl_vtable;

  GST_TRACE ("Deleting FBO %u", del_fbo);

  if (del_fbo) {
    gl->DeleteFramebuffers (1, &del_fbo);
    del_fbo = 0;
  }
  if (del_depth_buffer) {
    gl->DeleteRenderbuffers (1, &del_depth_buffer);
    del_depth_buffer = 0;
  }
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
        g_object_unref (G_OBJECT (gen_shader));
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
    g_object_unref (G_OBJECT (del_shader));
    del_shader = NULL;
  }
}
