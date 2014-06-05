/*
 * Copyright (C) 2014, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamc2dtexturerenderer.h"

/* *INDENT-OFF* */

static const gchar frag_COPY_OES[] =
    "#extension GL_OES_EGL_image_external : require \n"
    "precision mediump float;                       \n"
    "varying vec2 v_texcoord;                       \n"
    "uniform samplerExternalOES u_tex;              \n"
    "void main (void)                               \n"
    "{                                              \n"
    "  vec4 t = texture2D(u_tex, v_texcoord);       \n"
    "  gl_FragColor = vec4(t.rgb, 1.0);             \n"
    "}";

static const gchar vert_COPY_OES[] =
    "attribute vec4 a_position;                                         \n"
    "attribute vec2 a_texcoord;                                         \n"
    "varying vec2 v_texcoord;                                           \n"
    "uniform mat4 u_transformation;                                     \n"
    "void main()                                                        \n"
    "{                                                                  \n"
    "  gl_Position = a_position;                                        \n"
    "  v_texcoord = (u_transformation * vec4(a_texcoord, 0, 1)).xy;     \n"
    "}";

/* *INDENT-ON* */
static void
_surface_texture_detach_from_gl_context (GstGLContext * context,
    GstAmc2DTextureRenderer * renderer)
{
  renderer->gl_context_result =
      gst_amc_surface_texture_detach_from_gl_context (renderer->surface_texture,
      &renderer->gl_context_error);
}

static gboolean
_surface_texture_detach_from_gl_context_perform (GstAmc2DTextureRenderer *
    renderer, GError ** error)
{
  renderer->gl_context_result = FALSE;
  renderer->gl_context_error = NULL;

  gst_gl_context_thread_add (renderer->context,
      (GstGLContextThreadFunc) _surface_texture_detach_from_gl_context,
      renderer);

  *error = renderer->gl_context_error;
  renderer->gl_context_error = NULL;

  return renderer->gl_context_result;
}

static void
_gen_oes_texture (GstGLContext * context, guint * tex_id)
{
  const GstGLFuncs *gl = context->gl_vtable;

  GST_TRACE ("Generating OES texture");

  gl->GenTextures (1, tex_id);
  gl->BindTexture (GL_TEXTURE_EXTERNAL_OES, *tex_id);

  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  gl->BindTexture (GL_TEXTURE_EXTERNAL_OES, 0);

  GST_LOG ("generated OES texture id:%d", *tex_id);
}

GstAmc2DTextureRenderer *
gst_amc_2d_texture_renderer_new (GstGLContext * context,
    GstAmcSurfaceTexture * surface_texture, guint width, guint height)
{
  GstAmc2DTextureRenderer *renderer;

  g_return_val_if_fail (surface_texture != NULL, NULL);

  renderer = g_new0 (GstAmc2DTextureRenderer, 1);
  if (!renderer)
    return NULL;

  renderer->context = gst_object_ref (context);
  renderer->surface_texture = g_object_ref (surface_texture);

  gst_video_info_init (&renderer->info);
  gst_video_info_set_format (&renderer->info,
      GST_VIDEO_FORMAT_RGBA, width, height);

  return renderer;
}

void
gst_amc_2d_texture_renderer_free (GstAmc2DTextureRenderer * renderer)
{
  GError *error = NULL;

  if (renderer->surface_texture) {
    _surface_texture_detach_from_gl_context_perform (renderer, &error);
    g_object_unref (renderer->surface_texture);
  }

  if (renderer->fbo || renderer->depth_buffer) {
    gst_gl_context_del_fbo (renderer->context, renderer->fbo,
        renderer->depth_buffer);
  }

  if (renderer->shader) {
    gst_object_unref (renderer->shader);
  }

  if (renderer->oes_tex_id) {
    gst_gl_context_del_texture (renderer->context, &renderer->oes_tex_id);
  }

  if (renderer->context) {
    gst_object_unref (renderer->context);
  }

  g_free (renderer);
}

static gboolean
_2d_texture_renderer_init_fbo (GstAmc2DTextureRenderer * renderer)
{
  const GstGLFuncs *gl;
  GLuint fake_texture = 0;
  guint out_width, out_height;
  guint internal_format;

  out_width = GST_VIDEO_INFO_WIDTH (&renderer->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&renderer->info);
  internal_format =
      gst_gl_sized_gl_format_from_gl_format_type (renderer->context, GL_RGBA,
      GL_UNSIGNED_BYTE);

  gl = renderer->context->gl_vtable;

  if (!gl->GenFramebuffers) {
    /* turn off the pipeline because Frame buffer object is a not present */
    gst_gl_context_set_error (renderer->context,
        "Context, EXT_framebuffer_object supported: no");
    return FALSE;
  }

  GST_INFO ("Context, EXT_framebuffer_object supported: yes");

  /* setup FBO */
  gl->GenFramebuffers (1, &renderer->fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, renderer->fbo);

  /* setup the render buffer for depth */
  gl->GenRenderbuffers (1, &renderer->depth_buffer);
  gl->BindRenderbuffer (GL_RENDERBUFFER, renderer->depth_buffer);
  gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
      out_width, out_height);

  /* a fake texture is attached to the render FBO (cannot init without it) */
  gl->GenTextures (1, &fake_texture);
  gl->BindTexture (GL_TEXTURE_2D, fake_texture);
  gl->TexImage2D (GL_TEXTURE_2D, 0, internal_format, out_width, out_height,
      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_RENDERBUFFER, renderer->depth_buffer);

  if (!gst_gl_context_check_framebuffer_status (renderer->context)) {
    gst_gl_context_set_error (renderer->context,
        "GL framebuffer status incomplete");
    return FALSE;
  }

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
  gl->DeleteTextures (1, &fake_texture);

  return TRUE;
}

static gboolean
_2d_texture_renderer_init (GstAmc2DTextureRenderer * renderer)
{
  GstGLFuncs *gl;
  gboolean res;

  gl = renderer->context->gl_vtable;

  if (renderer->initialized)
    return TRUE;

  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    gst_gl_context_set_error (renderer->context,
        "Cannot perform conversion without OpenGL shaders");
    return FALSE;
  }

  _gen_oes_texture (renderer->context, &renderer->oes_tex_id);

  res =
      gst_gl_context_gen_shader (renderer->context, vert_COPY_OES,
      frag_COPY_OES, &renderer->shader);
  if (!res)
    return FALSE;

  renderer->shader_attr_position_loc =
      gst_gl_shader_get_attribute_location (renderer->shader, "a_position");
  renderer->shader_attr_texture_loc =
      gst_gl_shader_get_attribute_location (renderer->shader, "a_texcoord");

  gst_gl_shader_use (renderer->shader);

  gst_gl_shader_set_uniform_1i (renderer->shader, "u_tex", 0);

  gst_gl_context_clear_shader (renderer->context);

  if (!_2d_texture_renderer_init_fbo (renderer))
    return FALSE;

  gl->BindTexture (GL_TEXTURE_2D, 0);

  renderer->initialized = TRUE;

  return TRUE;
}

static gboolean
_2d_texture_renderer_draw (GstAmc2DTextureRenderer * renderer)
{
  GstGLFuncs *gl;
  guint out_width, out_height;

  GLint viewport_dim[4];

  /* *INDENT-OFF* */
  const GLfloat vertices[] = {
    1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f
  };
  /* *INDENT-ON* */

  GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0,
  };

  gl = renderer->context->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&renderer->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&renderer->info);

  gl->BindFramebuffer (GL_FRAMEBUFFER, renderer->fbo);

  /* attach the texture to the FBO to rendererer to */
  gl->BindTexture (GL_TEXTURE_2D, renderer->tex_id);
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, renderer->tex_id, 0);

  if (gl->DrawBuffers)
    gl->DrawBuffers (1, multipleRT);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, out_width, out_height);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (renderer->shader);
  gst_gl_shader_set_uniform_matrix_4fv (renderer->shader, "u_transformation", 1,
      FALSE, renderer->transformation_matrix);

  gl->VertexAttribPointer (renderer->shader_attr_position_loc, 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vertices);
  gl->VertexAttribPointer (renderer->shader_attr_texture_loc, 2,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vertices[3]);

  gl->EnableVertexAttribArray (renderer->shader_attr_position_loc);
  gl->EnableVertexAttribArray (renderer->shader_attr_texture_loc);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_EXTERNAL_OES, renderer->oes_tex_id);

  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  gl->DisableVertexAttribArray (renderer->shader_attr_position_loc);
  gl->DisableVertexAttribArray (renderer->shader_attr_texture_loc);

  if (gl->DrawBuffer)
    gl->DrawBuffer (GL_NONE);

  /* we are done with the shader */
  gst_gl_context_clear_shader (renderer->context);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  gst_gl_context_check_framebuffer_status (renderer->context);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  return TRUE;
}

static void
_2d_texture_renderer_render (GstGLContext * context,
    GstAmc2DTextureRenderer * renderer)
{
  gfloat identify_matrix[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };

  gfloat yflip_matrix[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, -1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 1.0f
  };

  gfloat transformation_matrix[16] = { 0 };

  if (!renderer->initialized) {
    if (!_2d_texture_renderer_init (renderer)) {
      renderer->result = FALSE;
      return;
    }

    if (!gst_amc_surface_texture_attach_to_gl_context
        (renderer->surface_texture, renderer->oes_tex_id,
            &renderer->gl_context_error)) {
      renderer->result = FALSE;
      return;
    }
  }

  if (!gst_amc_surface_texture_update_tex_image (renderer->surface_texture,
          &renderer->gl_context_error)) {
    renderer->result = FALSE;
    return;
  }

  if (gst_amc_surface_texture_get_transform_matrix (renderer->surface_texture,
          transformation_matrix, &renderer->gl_context_error)) {
    int i, j;

    for (i = 0; i < 16; i += 4) {
      renderer->transformation_matrix[i + j] = 0.0f;
      for (j = 0; j < 4; ++j) {
        renderer->transformation_matrix[i + j] =
            (transformation_matrix[i + 0] * yflip_matrix[j + 0])
            + (transformation_matrix[i + 1] * yflip_matrix[j + 4])
            + (transformation_matrix[i + 2] * yflip_matrix[j + 8])
            + (transformation_matrix[i + 3] * yflip_matrix[j + 12]);
      }
    }
  } else {
    memcpy (renderer->transformation_matrix, identify_matrix,
        sizeof (identify_matrix[0] * 16));
  }

  if (!_2d_texture_renderer_draw (renderer)) {
    renderer->result = FALSE;
    return;
  }

  renderer->result = TRUE;
}

gboolean
gst_amc_2d_texture_renderer_render (GstAmc2DTextureRenderer *
    renderer, guint tex_id, GError ** error)
{
  g_return_val_if_fail (renderer != NULL, FALSE);
  g_return_val_if_fail (tex_id, FALSE);

  renderer->tex_id = tex_id;

  renderer->result = FALSE;
  renderer->gl_context_error = NULL;

  gst_gl_context_thread_add (renderer->context,
      (GstGLContextThreadFunc) _2d_texture_renderer_render, renderer);

  *error = renderer->gl_context_error;
  renderer->gl_context_error = NULL;

  return renderer->result;
}
