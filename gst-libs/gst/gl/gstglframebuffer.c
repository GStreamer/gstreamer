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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gl.h"
#include "gstglframebuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_framebuffer_debug);
#define GST_CAT_DEFAULT gst_gl_framebuffer_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_framebuffer_debug, "glframebuffer", 0, "GL Framebuffer");

G_DEFINE_TYPE_WITH_CODE (GstGLFramebuffer, gst_gl_framebuffer, G_TYPE_OBJECT,
    DEBUG_INIT);

#define GST_GL_FRAMEBUFFER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_FRAMEBUFFER, GstGLFramebufferPrivate))

static void gst_gl_framebuffer_finalize (GObject * object);

struct _GstGLFramebufferPrivate
{
  gint width;
  gint height;

  guint fbo;
  guint depth;
};

static void
gst_gl_framebuffer_class_init (GstGLFramebufferClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLFramebufferPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_framebuffer_finalize;
}

static void
gst_gl_framebuffer_init (GstGLFramebuffer * fbo)
{
  fbo->priv = GST_GL_FRAMEBUFFER_GET_PRIVATE (fbo);
}

static void
gst_gl_framebuffer_finalize (GObject * object)
{
  GstGLFramebuffer *fbo = GST_GL_FRAMEBUFFER (object);

  if (fbo->context) {
    gst_object_unref (fbo->context);
    fbo->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_framebuffer_parent_class)->finalize (object);
}

GstGLFramebuffer *
gst_gl_framebuffer_new (GstGLContext * context)
{
  GstGLFramebuffer *fbo = g_object_new (GST_TYPE_GL_FRAMEBUFFER, NULL);

  fbo->context = gst_object_ref (context);

  return fbo;
}

gboolean
gst_gl_framebuffer_generate (GstGLFramebuffer * frame, gint width, gint height,
    guint * fbo, guint * depth)
{
  GLuint fake_texture = 0;
  const GstGLFuncs *gl;

  g_return_val_if_fail (GST_IS_GL_FRAMEBUFFER (frame), FALSE);
  g_return_val_if_fail (fbo != NULL && depth != NULL, FALSE);
  g_return_val_if_fail (width > 0 && height > 0, FALSE);

  gl = frame->context->gl_vtable;

  GST_TRACE ("creating FBO dimensions:%ux%u", width, height);

  if (!gl->GenFramebuffers) {
    gst_gl_context_set_error (frame->context,
        "Context, EXT_framebuffer_object not supported");
    return FALSE;
  }
  /* setup FBO */
  gl->GenFramebuffers (1, fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, *fbo);

  /* setup the render buffer for depth */
  gl->GenRenderbuffers (1, depth);
  gl->BindRenderbuffer (GL_RENDERBUFFER, *depth);

  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
        width, height);
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
        width, height);
  }
  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_GLES2) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
        width, height);
  }

  /* setup a texture to render to */
  gl->GenTextures (1, &fake_texture);
  gl->BindTexture (GL_TEXTURE_2D, fake_texture);
  gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
      width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_RENDERBUFFER, *depth);

  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL) {
    gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, *depth);
  }

  if (gl->CheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    gst_gl_context_set_error (frame->context,
        "GL framebuffer status incomplete");

    gl->DeleteTextures (1, &fake_texture);

    return FALSE;
  }

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteTextures (1, &fake_texture);

  return TRUE;
}

gboolean
gst_gl_framebuffer_use (GstGLFramebuffer * frame, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB cb, gint input_tex_width,
    gint input_tex_height, GLuint input_tex, gdouble proj_param1,
    gdouble proj_param2, gdouble proj_param3, gdouble proj_param4,
    GstGLDisplayProjection projection, gpointer stuff)
{
  const GstGLFuncs *gl;
#if GST_GL_HAVE_GLES2
  GLint viewport_dim[4];
#endif

  g_return_val_if_fail (GST_IS_GL_FRAMEBUFFER (frame), FALSE);
  g_return_val_if_fail (input_tex_width > 0 && input_tex_height > 0, FALSE);
  g_return_val_if_fail (texture_fbo_width > 0 && texture_fbo_height > 0, FALSE);
  g_return_val_if_fail (input_tex != 0, FALSE);
  g_return_val_if_fail (fbo != 0, FALSE);
  g_return_val_if_fail (texture_fbo != 0, FALSE);
  g_return_val_if_fail (cb != NULL, FALSE);

  gl = frame->context->gl_vtable;

  GST_TRACE ("Binding v1 FBO %u dimensions:%ux%u with texture:%u "
      "dimensions:%ux%u", fbo, texture_fbo_width,
      texture_fbo_height, texture_fbo, input_tex_width, input_tex_height);

  gl->BindFramebuffer (GL_FRAMEBUFFER, fbo);

  /*setup a texture to render to */
  gl->BindTexture (GL_TEXTURE_2D, texture_fbo);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, texture_fbo, 0);

  gst_gl_context_clear_shader (frame->context);

#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL) {
    gl->PushAttrib (GL_VIEWPORT_BIT);
    gl->MatrixMode (GL_PROJECTION);
    gl->PushMatrix ();
    gl->LoadIdentity ();

    switch (projection) {
      case GST_GL_DISPLAY_PROJECTION_ORTHO2D:
        gluOrtho2D (proj_param1, proj_param2, proj_param3, proj_param4);
        break;
      case GST_GL_DISPLAY_PROJECTION_PERSPECTIVE:
        gluPerspective (proj_param1, proj_param2, proj_param3, proj_param4);
        break;
      default:
        gst_gl_context_set_error (frame->context, "Unknow fbo projection %d",
            projection);
    }

    gl->MatrixMode (GL_MODELVIEW);
    gl->PushMatrix ();
    gl->LoadIdentity ();
  }
#endif
#if GST_GL_HAVE_GLES2
  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_GLES2)
    gl->GetIntegerv (GL_VIEWPORT, viewport_dim);
#endif

  gl->Viewport (0, 0, texture_fbo_width, texture_fbo_height);

#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL) {
    const GLenum rt[] = { GL_COLOR_ATTACHMENT0 };
    gl->DrawBuffers (1, rt);
  }
#endif

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  cb (input_tex_width, input_tex_height, input_tex, stuff);

#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL) {
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
  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_GLES2) {
    gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
        viewport_dim[3]);
  }
#endif

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  return TRUE;
}

gboolean
gst_gl_framebuffer_use_v2 (GstGLFramebuffer * frame, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB_V2 cb, gpointer stuff)
{
  const GstGLFuncs *gl;
  GLint viewport_dim[4];

  g_return_val_if_fail (GST_IS_GL_FRAMEBUFFER (frame), FALSE);
  g_return_val_if_fail (texture_fbo_width > 0 && texture_fbo_height > 0, FALSE);
  g_return_val_if_fail (fbo != 0, FALSE);
  g_return_val_if_fail (texture_fbo != 0, FALSE);
  g_return_val_if_fail (cb != NULL, FALSE);

  gl = frame->context->gl_vtable;

  GST_TRACE ("Binding v2 FBO %u dimensions:%ux%u with texture:%u ",
      fbo, texture_fbo_width, texture_fbo_height, texture_fbo);

  gl->BindFramebuffer (GL_FRAMEBUFFER, fbo);

  /* setup a texture to render to */
  gl->BindTexture (GL_TEXTURE_2D, texture_fbo);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, texture_fbo, 0);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, texture_fbo_width, texture_fbo_height);

  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* the opengl scene */
  cb (stuff);

  if (gst_gl_context_get_gl_api (frame->context) & GST_GL_API_OPENGL)
    gl->DrawBuffer (GL_NONE);

  gl->Viewport (viewport_dim[0], viewport_dim[1],
      viewport_dim[2], viewport_dim[3]);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  return TRUE;
}

void
gst_gl_framebuffer_delete (GstGLFramebuffer * frame, guint fbo, guint depth)
{
  const GstGLFuncs *gl;

  g_return_if_fail (GST_IS_GL_FRAMEBUFFER (frame));

  gl = frame->context->gl_vtable;

  GST_TRACE ("Deleting FBO %u", fbo);

  if (fbo) {
    gl->DeleteFramebuffers (1, &fbo);
  }
  if (depth) {
    gl->DeleteRenderbuffers (1, &depth);
  }
}
