/*
 * GStreamer EGL/GLES Sink Adaptation
 * Copyright (C) 2012-2013 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *   @author: Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstegladaptation.h"

#include <gst/video/video.h>
#include <string.h>

#define GST_CAT_DEFAULT egladaption_debug
GST_DEBUG_CATEGORY (egladaption_debug);

/* GLESv2 GLSL Shaders
 *
 * OpenGL ES Standard does not mandate YUV support. This is
 * why most of these shaders deal with Packed/Planar YUV->RGB
 * conversion.
 */

/* *INDENT-OFF* */
/* Direct vertex copy */
static const char *vert_COPY_prog = {
      "attribute vec3 position;"
      "attribute vec2 texpos;"
      "varying vec2 opos;"
      "void main(void)"
      "{"
      " opos = texpos;"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

static const char *vert_COPY_prog_no_tex = {
      "attribute vec3 position;"
      "void main(void)"
      "{"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

/* Paint all black */
static const char *frag_BLACK_prog = {
  "precision mediump float;"
      "void main(void)"
      "{"
      " gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);"
      "}"
};

/* Direct fragments copy with stride-scaling */
static const char *frag_COPY_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.rgb, 1.0);"
      "}"
};

/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);"
      "}"
};

/* Packed YUV converters */

/** AYUV to RGB conversion */
static const char *frag_AYUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv  = texture2D(tex,opos / tex_scale0).gba;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/* Planar YUV converters */

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,Utex,Vtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.y=texture2D(Utex,opos / tex_scale1).r;"
      "  yuv.z=texture2D(Vtex,opos / tex_scale2).r;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,UVtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.yz=texture2D(UVtex,opos / tex_scale1).%c%c;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};
/* *INDENT-ON* */

void
gst_egl_adaption_init (void)
{
  GST_DEBUG_CATEGORY_INIT (egladaption_debug, "egladaption", 0,
      "EGL adaption layer");
}

static GstCaps *
_gst_video_format_new_template_caps (GstVideoFormat format)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

GstCaps *
gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext *
    ctx)
{
  GstCaps *caps = NULL, *copy1, *copy2;
  guint i, n;

  GST_DEBUG_OBJECT (ctx->element,
      "Building initial list of wanted eglattribs per format");

  /* Init supported format/caps list */
  if (_gst_egl_choose_config (ctx, TRUE, NULL)) {

    caps = gst_caps_new_empty ();

    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y42B));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB16));

    copy1 = gst_caps_copy (caps);
    copy2 = gst_caps_copy (caps);

    n = gst_caps_get_size (caps);
    for (i = 0; i < n; i++) {
      GstCapsFeatures *features =
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_EGL_IMAGE, NULL);
      gst_caps_set_features (caps, i, features);
    }

    n = gst_caps_get_size (copy1);
    for (i = 0; i < n; i++) {
      GstCapsFeatures *features =
          gst_caps_features_new
          (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, NULL);
      gst_caps_set_features (copy1, i, features);
    }

    gst_caps_append (caps, copy1);
    gst_caps_append (caps, copy2);
  } else {
    GST_INFO_OBJECT (ctx->element,
        "EGL display doesn't support RGBA8888 config");
  }

  return caps;
}

void
gst_egl_adaptation_cleanup (GstEglAdaptationContext * ctx)
{
  gint i;

  glUseProgram (0);

  if (ctx->have_vbo) {
    glDeleteBuffers (1, &ctx->position_buffer);
    glDeleteBuffers (1, &ctx->index_buffer);
    ctx->have_vbo = FALSE;
  }

  if (ctx->have_texture) {
    glDeleteTextures (ctx->n_textures, ctx->texture);
    ctx->have_texture = FALSE;
    ctx->n_textures = 0;
  }

  for (i = 0; i < 2; i++) {
    if (ctx->glslprogram[i]) {
      glDetachShader (ctx->glslprogram[i], ctx->fragshader[i]);
      glDetachShader (ctx->glslprogram[i], ctx->vertshader[i]);
      glDeleteProgram (ctx->glslprogram[i]);
      glDeleteShader (ctx->fragshader[i]);
      glDeleteShader (ctx->vertshader[i]);
      ctx->glslprogram[i] = 0;
      ctx->fragshader[i] = 0;
      ctx->vertshader[i] = 0;
    }
  }

  gst_egl_adaptation_context_make_current (ctx, FALSE);

  gst_egl_adaptation_destroy_surface (ctx);
  gst_egl_adaptation_destroy_context (ctx);
}

gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

static gboolean
create_shader_program (GstEglAdaptationContext * ctx, GLuint * prog,
    GLuint * vert, GLuint * frag, const gchar * vert_text,
    const gchar * frag_text)
{
  GLint test;
  GLchar *info_log;

  /* Build shader program for video texture rendering */
  *vert = glCreateShader (GL_VERTEX_SHADER);
  GST_DEBUG_OBJECT (ctx->element, "Sending %s to handle %d", vert_text, *vert);
  glShaderSource (*vert, 1, &vert_text, NULL);
  if (got_gl_error ("glShaderSource vertex"))
    goto HANDLE_ERROR;

  glCompileShader (*vert);
  if (got_gl_error ("glCompileShader vertex"))
    goto HANDLE_ERROR;

  glGetShaderiv (*vert, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (ctx->element, "Successfully compiled vertex shader");
  else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't compile vertex shader");
    glGetShaderiv (*vert, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*vert, test, NULL, info_log);
    GST_INFO_OBJECT (ctx->element, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *frag = glCreateShader (GL_FRAGMENT_SHADER);
  GST_DEBUG_OBJECT (ctx->element, "Sending %s to handle %d", frag_text, *frag);
  glShaderSource (*frag, 1, &frag_text, NULL);
  if (got_gl_error ("glShaderSource fragment"))
    goto HANDLE_ERROR;

  glCompileShader (*frag);
  if (got_gl_error ("glCompileShader fragment"))
    goto HANDLE_ERROR;

  glGetShaderiv (*frag, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (ctx->element, "Successfully compiled fragment shader");
  else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't compile fragment shader");
    glGetShaderiv (*frag, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*frag, test, NULL, info_log);
    GST_INFO_OBJECT (ctx->element, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *prog = glCreateProgram ();
  if (got_gl_error ("glCreateProgram"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *vert);
  if (got_gl_error ("glAttachShader vertices"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *frag);
  if (got_gl_error ("glAttachShader fragments"))
    goto HANDLE_ERROR;
  glLinkProgram (*prog);
  glGetProgramiv (*prog, GL_LINK_STATUS, &test);
  if (test != GL_FALSE) {
    GST_DEBUG_OBJECT (ctx->element, "GLES: Successfully linked program");
  } else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't link program");
    goto HANDLE_ERROR;
  }

  return TRUE;

HANDLE_ERROR:
  {
    if (*frag && *prog)
      glDetachShader (*prog, *frag);
    if (*vert && *prog)
      glDetachShader (*prog, *vert);
    if (*prog)
      glDeleteProgram (*prog);
    if (*frag)
      glDeleteShader (*frag);
    if (*vert)
      glDeleteShader (*vert);
    *prog = 0;
    *frag = 0;
    *vert = 0;

    return FALSE;
  }
}

gboolean
gst_egl_adaptation_init_egl_surface (GstEglAdaptationContext * ctx,
    GstVideoFormat format)
{
  GLboolean ret;
  const gchar *texnames[3] = { NULL, };
  gchar *frag_prog = NULL;
  gboolean free_frag_prog = FALSE;
  gint i;

  GST_DEBUG_OBJECT (ctx->element, "Enter EGL surface setup");

  if (!gst_egl_adaptation_create_surface (ctx)) {
    GST_ERROR_OBJECT (ctx->element, "Can't create surface");
    goto HANDLE_ERROR_LOCKED;
  }

  if (!gst_egl_adaptation_context_make_current (ctx, TRUE))
    goto HANDLE_ERROR_LOCKED;

  gst_egl_adaptation_query_buffer_preserved (ctx);

  gst_egl_adaptation_init_egl_exts (ctx);

  /* Save surface dims */
  gst_egl_adaptation_update_surface_dimensions (ctx);

  /* Save display's pixel aspect ratio
   *
   * DAR is reported as w/h * EGL_DISPLAY_SCALING wich is
   * a constant with value 10000. This attribute is only
   * supported if the EGL version is >= 1.2
   * XXX: Setup this as a property.
   * or some other one time check. Right now it's being called once
   * per frame.
   */
  gst_egl_adaptation_query_par (ctx);

  /* We have a surface! */
  ctx->have_surface = TRUE;

  /* Init vertex and fragment GLSL shaders. 
   * Note: Shader compiler support is optional but we currently rely on it.
   */

  glGetBooleanv (GL_SHADER_COMPILER, &ret);
  if (ret == GL_FALSE) {
    GST_ERROR_OBJECT (ctx->element, "Shader compiler support is unavailable!");
    goto HANDLE_ERROR;
  }

  /* Build shader program for video texture rendering */

  switch (format) {
    case GST_VIDEO_FORMAT_AYUV:
      frag_prog = (gchar *) frag_AYUV_prog;
      free_frag_prog = FALSE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      frag_prog = (gchar *) frag_PLANAR_YUV_prog;
      free_frag_prog = FALSE;
      ctx->n_textures = 3;
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_NV12:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a');
      free_frag_prog = TRUE;
      ctx->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
      free_frag_prog = TRUE;
      ctx->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
      free_frag_prog = TRUE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
      free_frag_prog = TRUE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
      free_frag_prog = TRUE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB16:
      frag_prog = (gchar *) frag_COPY_prog;
      free_frag_prog = FALSE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (!create_shader_program (ctx,
          &ctx->glslprogram[0],
          &ctx->vertshader[0],
          &ctx->fragshader[0], vert_COPY_prog, frag_prog)) {
    if (free_frag_prog)
      g_free (frag_prog);
    frag_prog = NULL;
    goto HANDLE_ERROR;
  }
  if (free_frag_prog)
    g_free (frag_prog);
  frag_prog = NULL;

  ctx->position_loc[0] = glGetAttribLocation (ctx->glslprogram[0], "position");
  ctx->texpos_loc[0] = glGetAttribLocation (ctx->glslprogram[0], "texpos");
  ctx->tex_scale_loc[0][0] =
      glGetUniformLocation (ctx->glslprogram[0], "tex_scale0");
  ctx->tex_scale_loc[0][1] =
      glGetUniformLocation (ctx->glslprogram[0], "tex_scale1");
  ctx->tex_scale_loc[0][2] =
      glGetUniformLocation (ctx->glslprogram[0], "tex_scale2");

  glEnableVertexAttribArray (ctx->position_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  glEnableVertexAttribArray (ctx->texpos_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  for (i = 0; i < ctx->n_textures; i++) {
    ctx->tex_loc[0][i] =
        glGetUniformLocation (ctx->glslprogram[0], texnames[i]);
  }

  if (!ctx->buffer_preserved) {
    /* Build shader program for black borders */
    if (!create_shader_program (ctx,
            &ctx->glslprogram[1],
            &ctx->vertshader[1],
            &ctx->fragshader[1], vert_COPY_prog_no_tex, frag_BLACK_prog))
      goto HANDLE_ERROR;

    ctx->position_loc[1] =
        glGetAttribLocation (ctx->glslprogram[1], "position");

    glEnableVertexAttribArray (ctx->position_loc[1]);
    if (got_gl_error ("glEnableVertexAttribArray"))
      goto HANDLE_ERROR;
  }

  /* Generate textures */
  if (!ctx->have_texture) {
    GST_INFO_OBJECT (ctx->element, "Performing initial texture setup");

    glGenTextures (ctx->n_textures, ctx->texture);
    if (got_gl_error ("glGenTextures"))
      goto HANDLE_ERROR_LOCKED;

    for (i = 0; i < ctx->n_textures; i++) {
      glBindTexture (GL_TEXTURE_2D, ctx->texture[i]);
      if (got_gl_error ("glBindTexture"))
        goto HANDLE_ERROR;

      /* Set 2D resizing params */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      /* If these are not set the texture image unit will return
       * (R, G, B, A) = black on glTexImage2D for non-POT width/height
       * frames. For a deeper explanation take a look at the OpenGL ES
       * documentation for glTexParameter */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (got_gl_error ("glTexParameteri"))
        goto HANDLE_ERROR_LOCKED;
    }

    ctx->have_texture = TRUE;
  }

  glUseProgram (0);

  return TRUE;

  /* Errors */
HANDLE_ERROR_LOCKED:
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup EGL surface");
  return FALSE;
}

gboolean
gst_egl_adaptation_choose_config (GstEglAdaptationContext * ctx)
{
  gint egl_configs;

  if (!_gst_egl_choose_config (ctx, FALSE, &egl_configs)) {
    GST_ERROR_OBJECT (ctx->element, "eglChooseConfig failed");
    goto HANDLE_ERROR;
  }

  if (egl_configs < 1) {
    GST_ERROR_OBJECT (ctx->element,
        "Could not find matching framebuffer config");
    goto HANDLE_ERROR;
  }

  if (!gst_egl_adaptation_create_egl_context (ctx)) {
    GST_ERROR_OBJECT (ctx->element, "Error getting context, eglCreateContext");
    goto HANDLE_ERROR;
  }

  return TRUE;

  /* Errors */
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't choose an usable config");
  return FALSE;
}

GstEglAdaptationContext *
gst_egl_adaptation_context_new (GstElement * element)
{
  GstEglAdaptationContext *ctx = g_new0 (GstEglAdaptationContext, 1);

  ctx->element = gst_object_ref (element);

  gst_egl_adaptation_init (ctx);
  return ctx;
}

void
gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx)
{
  gst_egl_adaptation_deinit (ctx);
  gst_object_unref (ctx->element);
  g_free (ctx);
}
