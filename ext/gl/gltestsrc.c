/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gltestsrc.h"

struct vts_color_struct
{
  gfloat R, G, B;
};

enum
{
  COLOR_WHITE = 0,
  COLOR_YELLOW,
  COLOR_CYAN,
  COLOR_GREEN,
  COLOR_MAGENTA,
  COLOR_RED,
  COLOR_BLUE,
  COLOR_BLACK,
  COLOR_NEG_I,
  COLOR_POS_Q,
  COLOR_SUPER_BLACK,
  COLOR_DARK_GREY
};

static const struct vts_color_struct vts_colors[] = {
  /* 100% white */
  {1.0f, 1.0f, 1.0f},
  /* yellow */
  {1.0f, 1.0f, 0.0f},
  /* cyan */
  {0.0f, 1.0f, 1.0f},
  /* green */
  {0.0f, 1.0f, 0.0f},
  /* magenta */
  {1.0f, 0.0f, 1.0f},
  /* red */
  {1.0f, 0.0f, 0.0f},
  /* blue */
  {0.0f, 0.0f, 1.0f},
  /* black */
  {0.0f, 0.0f, 0.0f},
  /* -I */
  {0.0, 0.0f, 0.5f},
  /* +Q */
  {0.0f, 0.5, 1.0f},
  /* superblack */
  {0.0f, 0.0f, 0.0f},
  /* 7.421875% grey */
  {19. / 256.0f, 19. / 256.0f, 19. / 256.0},
};

/* *INDENT-OFF* */
static const GLfloat positions[] = {
     -1.0,  1.0,  0.0, 1.0,
      1.0,  1.0,  0.0, 1.0,
      1.0, -1.0,  0.0, 1.0,
     -1.0, -1.0,  0.0, 1.0,
};

static const GLushort indices_quad[] = { 0, 1, 2, 0, 2, 3 };
/* *INDENT-ON* */

struct SrcSMPTE
{
  struct BaseSrcImpl base;
};

static gpointer
_src_smpte_new (GstGLTestSrc * test)
{
  struct SrcSMPTE *src = g_new0 (struct SrcSMPTE, 1);

  src->base.src = test;

  return src;
}

static gboolean
_src_smpte_init (gpointer impl, GstGLContext * context, GstVideoInfo * v_info)
{
  struct SrcSMPTE *src = impl;

  src->base.context = context;

  return TRUE;
}

static gboolean
_src_smpte_fill_bound_fbo (gpointer impl)
{
#if GST_GL_HAVE_OPENGL
  struct SrcSMPTE *src = impl;
  int i;

  if (gst_gl_context_get_gl_api (src->base.context) & GST_GL_API_OPENGL) {

    glClearColor (0.0, 0.0, 0.0, 1.0);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable (GL_CULL_FACE);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    for (i = 0; i < 7; i++) {
      glColor4f (vts_colors[i].R, vts_colors[i].G, vts_colors[i].B, 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f + 2.0 * (2.0f / 3.0f), 0);
      glVertex3f (-1.0f + (i + 1.0f) * (2.0f / 7.0f),
          -1.0f + 2.0f * (2.0f / 3.0f), 0);
      glVertex3f (-1.0f + (i + 1.0f) * (2.0f / 7.0f), -1.0f, 0);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f, 0);
      glEnd ();
    }

    for (i = 0; i < 7; i++) {
      int k;

      if (i & 1) {
        k = 7;
      } else {
        k = 6 - i;
      }

      glColor4f (vts_colors[k].R, vts_colors[k].G, vts_colors[k].B, 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 7.0f), -1.0f + 2.0f * (3.0f / 4.0f),
          0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 7.0f), -1.0f + 2.0f * (2.0f / 3.0f),
          0);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f + 2.0f * (2.0f / 3.0f), 0);
      glEnd ();
    }

    for (i = 0; i < 3; i++) {
      int k;

      if (i == 0) {
        k = 8;
      } else if (i == 1) {
        k = 0;
      } else {
        k = 9;
      }

      glColor4f (vts_colors[k].R, vts_colors[k].G, vts_colors[k].B, 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + i * (2.0f / 6.0f), -1.0f + 2.0f * 1, 0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 6.0f), -1.0f + 2.0f * 1, 0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 6.0f), -1.0f + 2.0f * (3.0f / 4.0f),
          0);
      glVertex3f (-1.0f + i * (2.0f / 6.0f), -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glEnd ();
    }

    for (i = 0; i < 3; i++) {
      int k;

      if (i == 0) {
        k = COLOR_SUPER_BLACK;
      } else if (i == 1) {
        k = COLOR_BLACK;
      } else {
        k = COLOR_DARK_GREY;
      }

      glColor4f (vts_colors[k].R, vts_colors[k].G, vts_colors[k].B, 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + 2.0f * (0.5f + i * (1.0f / 12.0f)), -1.0 + 2.0f * 1,
          0);
      glVertex3f (-1.0f + 2.0f * (0.5f + (i + 1) * (1.0f / 12.0f)),
          -1.0f + 2.0f * 1, 0);
      glVertex3f (-1.0f + 2.0f * (0.5f + (i + 1) * (1.0f / 12.0f)),
          -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glVertex3f (-1.0f + 2.0f * (0.5f + i * (1.0f / 12.0f)),
          -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glEnd ();
    }

    glColor4f (1.0, 1.0, 1.0, 1.0);
    glBegin (GL_QUADS);
    glVertex3f (-1.0 + 2.0 * (0.75), -1.0 + 2.0 * 1, 0);
    glVertex3f (-1.0 + 2.0 * (1.0), -1.0 + 2.0 * 1, 0);
    glVertex3f (-1.0 + 2.0 * (1.0), -1.0 + 2.0 * (3.0 / 4.0), 0);
    glVertex3f (-1.0 + 2.0 * (0.75), -1.0 + 2.0 * (3.0 / 4.0), 0);
    glEnd ();
  }
#endif

  return TRUE;
}

static void
_src_smpte_free (gpointer impl)
{
  g_free (impl);
}


static const struct SrcFuncs src_smpte = {
  GST_GL_TEST_SRC_SMPTE,
  _src_smpte_new,
  _src_smpte_init,
  _src_smpte_fill_bound_fbo,
  _src_smpte_free,
};

struct SrcUniColor
{
  struct BaseSrcImpl base;

  struct vts_color_struct color;
};

static gpointer
_src_uni_color_new (GstGLTestSrc * test)
{
  struct SrcUniColor *src = g_new0 (struct SrcUniColor, 1);

  src->base.src = test;

  return src;
}

static gboolean
_src_uni_color_init (gpointer impl, GstGLContext * context,
    GstVideoInfo * v_info)
{
  struct SrcUniColor *src = impl;

  src->base.context = context;
  src->base.v_info = *v_info;

  return TRUE;
}

static gboolean
_src_uni_color_fill_bound_fbo (gpointer impl)
{
  struct SrcUniColor *src = impl;
  const GstGLFuncs *gl = src->base.context->gl_vtable;

  gl->ClearColor (src->color.R, src->color.G, src->color.B, 1.0f);
  gl->Clear (GL_COLOR_BUFFER_BIT);

  return TRUE;
}

static void
_src_uni_color_free (gpointer impl)
{
  g_free (impl);
}

#define SRC_UNICOLOR(name, cap_name) \
static gpointer \
G_PASTE(G_PASTE(_src_unicolor_,name),_new) (GstGLTestSrc * test) \
{ \
  struct SrcUniColor *src = _src_uni_color_new (test); \
  src->color = vts_colors[G_PASTE(COLOR_,cap_name)]; \
  return src; \
} \
static const struct SrcFuncs G_PASTE (src_,name) = { \
  G_PASTE(GST_GL_TEST_SRC_,cap_name), \
  G_PASTE(G_PASTE(_src_unicolor_,name),_new), \
  _src_uni_color_init, \
  _src_uni_color_fill_bound_fbo, \
  _src_uni_color_free, \
}

SRC_UNICOLOR (white, WHITE);
SRC_UNICOLOR (black, BLACK);
SRC_UNICOLOR (red, RED);
SRC_UNICOLOR (green, GREEN);
SRC_UNICOLOR (blue, BLUE);

static gpointer
_src_blink_new (GstGLTestSrc * test)
{
  struct SrcUniColor *src = _src_uni_color_new (test);

  src->color = vts_colors[COLOR_WHITE];

  return src;
}

static gboolean
_src_blink_fill_bound_fbo (gpointer impl)
{
  struct SrcUniColor *src = impl;

  if (src->color.R > 0.5) {
    src->color = vts_colors[COLOR_BLACK];
  } else {
    src->color = vts_colors[COLOR_WHITE];
  }

  return _src_uni_color_fill_bound_fbo (impl);
}

static const struct SrcFuncs src_blink = {
  GST_GL_TEST_SRC_BLINK,
  _src_blink_new,
  _src_uni_color_init,
  _src_blink_fill_bound_fbo,
  _src_uni_color_free,
};

struct SrcShader
{
  struct BaseSrcImpl base;

  GstGLShader *shader;

  gint attr_position;
  gint attr_texcoord;
  /* x, y, z, w */
  const gfloat *vertices;
  guint n_vertices;
  const gushort *indices;
  guint n_indices;
};

static gboolean
_src_shader_fill_bound_fbo (gpointer impl)
{
  struct SrcShader *src = impl;
  const GstGLFuncs *gl;

  g_return_val_if_fail (src->base.context, FALSE);
  g_return_val_if_fail (src->shader, FALSE);
  gl = src->base.context->gl_vtable;

  gst_gl_shader_use (src->shader);

  if (src->attr_position != -1) {
    gl->VertexAttribPointer (src->attr_position, 4, GL_FLOAT, GL_FALSE, 0,
        src->vertices);
    gl->EnableVertexAttribArray (src->attr_position);
  }
  gl->DrawElements (GL_TRIANGLES, src->n_indices, GL_UNSIGNED_SHORT,
      src->indices);

  if (src->attr_position != -1)
    gl->DisableVertexAttribArray (src->attr_position);
  gst_gl_context_clear_shader (src->base.context);

  return TRUE;
}

/* *INDENT-OFF* */
static const gchar *checkers_vertex_src = "attribute vec4 position;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = position;\n"
    "}";

static const gchar *checkers_fragment_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform float checker_width;\n"
    "void main()\n"
    "{\n"
    "  vec2 xy_index= floor((gl_FragCoord.xy-vec2(0.5,0.5))/checker_width);\n"
    "  vec2 xy_mod=mod(xy_index,vec2(2.0,2.0));\n"
    "  float result=mod(xy_mod.x+xy_mod.y,2.0);\n"
    "  gl_FragColor.r=step(result,0.5);\n"
    "  gl_FragColor.g=1.0-gl_FragColor.r;\n"
    "  gl_FragColor.ba=vec2(0,1);\n"
    "}";
/* *INDENT-ON* */

struct SrcCheckers
{
  struct SrcShader base;

  guint checker_width;
};

static gboolean
_src_checkers_init (gpointer impl, GstGLContext * context,
    GstVideoInfo * v_info)
{
  struct SrcCheckers *src = impl;
  GError *error = NULL;

  src->base.base.context = context;

  if (src->base.shader)
    gst_object_unref (src->base.shader);
  src->base.shader = gst_gl_shader_new_link_with_stages (context, &error,
      gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          checkers_vertex_src),
      gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          checkers_fragment_src), NULL);
  if (!src->base.shader) {
    GST_ERROR_OBJECT (src->base.base.src, "%s", error->message);
    return FALSE;
  }

  src->base.attr_position =
      gst_gl_shader_get_attribute_location (src->base.shader, "position");
  if (src->base.attr_position == -1) {
    GST_ERROR_OBJECT (src->base.base.src, "No position attribute");
    return FALSE;
  }
  src->base.vertices = positions;
  src->base.n_vertices = 4;
  src->base.indices = indices_quad;
  src->base.n_indices = 6;

  gst_gl_shader_use (src->base.shader);
  gst_gl_shader_set_uniform_1f (src->base.shader, "checker_width",
      src->checker_width);
  gst_gl_context_clear_shader (src->base.base.context);

  return TRUE;
}

static void
_src_checkers_free (gpointer impl)
{
  struct SrcCheckers *src = impl;

  if (!src)
    return;

  if (src->base.shader)
    gst_object_unref (src->base.shader);
  src->base.shader = NULL;

  g_free (impl);
}

static gpointer
_src_checkers_new (GstGLTestSrc * test)
{
  struct SrcCheckers *src = g_new0 (struct SrcCheckers, 1);

  src->base.base.src = test;

  return src;
}

#define SRC_CHECKERS(spacing) \
static gpointer \
G_PASTE(G_PASTE(_src_checkers,spacing),_new) (GstGLTestSrc * test) \
{ \
  struct SrcCheckers *src = _src_checkers_new (test); \
  src->checker_width = spacing; \
  return src; \
} \
static const struct SrcFuncs G_PASTE(src_checkers,spacing) = { \
  G_PASTE(GST_GL_TEST_SRC_CHECKERS,spacing), \
  G_PASTE(G_PASTE(_src_checkers,spacing),_new), \
  _src_checkers_init, \
  _src_shader_fill_bound_fbo, \
  _src_checkers_free, \
}

SRC_CHECKERS (1);
SRC_CHECKERS (2);
SRC_CHECKERS (4);
SRC_CHECKERS (8);

/* *INDENT-OFF* */
static const gchar *snow_vertex_src =
    "attribute vec4 position;\n"
    "varying vec2 out_uv;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = position;\n"
    "   out_uv = position.xy;\n"
    "}";

static const gchar *snow_fragment_src = 
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform float time;\n"
    "varying vec2 out_uv;\n"
    "\n"
    "float rand(vec2 co){\n"
    "    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);\n"
    "}\n"
    "void main()\n"
    "{\n"
    "  gl_FragColor = vec4(rand(time * out_uv));\n"
    "}";
/* *INDENT-ON* */

static gboolean
_src_snow_init (gpointer impl, GstGLContext * context, GstVideoInfo * v_info)
{
  struct SrcShader *src = impl;
  GError *error = NULL;

  src->base.context = context;

  if (src->shader)
    gst_object_unref (src->shader);
  src->shader = gst_gl_shader_new_link_with_stages (context, &error,
      gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          snow_vertex_src),
      gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          snow_fragment_src), NULL);
  if (!src->shader) {
    GST_ERROR_OBJECT (src->base.src, "%s", error->message);
    return FALSE;
  }

  src->attr_position =
      gst_gl_shader_get_attribute_location (src->shader, "position");
  if (src->attr_position == -1) {
    GST_ERROR_OBJECT (src->base.src, "No position attribute");
    return FALSE;
  }
  src->vertices = positions;
  src->n_vertices = 4;
  src->indices = indices_quad;
  src->n_indices = 6;

  return TRUE;
}

static gboolean
_src_snow_fill_bound_fbo (gpointer impl)
{
  struct SrcShader *src = impl;

  g_return_val_if_fail (src->base.context, FALSE);
  g_return_val_if_fail (src->shader, FALSE);

  gst_gl_shader_use (src->shader);
  gst_gl_shader_set_uniform_1f (src->shader, "time",
      (gfloat) src->base.src->running_time / GST_SECOND);

  return _src_shader_fill_bound_fbo (impl);
}

static void
_src_snow_free (gpointer impl)
{
  struct SrcShader *src = impl;

  if (!src)
    return;

  if (src->shader)
    gst_object_unref (src->shader);
  src->shader = NULL;

  g_free (impl);
}

static gpointer
_src_snow_new (GstGLTestSrc * test)
{
  struct SrcShader *src = g_new0 (struct SrcShader, 1);

  src->base.src = test;

  return src;
}

static const struct SrcFuncs src_snow = {
  GST_GL_TEST_SRC_SNOW,
  _src_snow_new,
  _src_snow_init,
  _src_snow_fill_bound_fbo,
  _src_snow_free,
};

/* *INDENT-OFF* */
static const gchar *mandelbrot_vertex_src = "attribute vec4 position;\n"
    "uniform float aspect_ratio;\n"
    "varying vec2 fractal_position;\n"
    "void main()\n"
    "{\n"
    "  gl_Position = position;\n"
    "  fractal_position = vec2(position.y * 0.5 - 0.3, aspect_ratio * position.x * 0.5);\n"
    "  fractal_position *= 2.5;\n"
    "}";

static const gchar *mandelbrot_fragment_src = 
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform float time;\n"
    "varying vec2 fractal_position;\n"
    "const vec4 K = vec4(1.0, 0.66, 0.33, 3.0);\n"
    "vec4 hsv_to_rgb(float hue, float saturation, float value) {\n"
    "  vec4 p = abs(fract(vec4(hue) + K) * 6.0 - K.wwww);\n"
    "  return value * mix(K.xxxx, clamp(p - K.xxxx, 0.0, 1.0), saturation);\n"
    "}\n"
    "vec4 i_to_rgb(int i) {\n"
    "  float hue = float(i) / 100.0 + sin(time);\n"
    "  return hsv_to_rgb(hue, 0.5, 0.8);\n"
    "}\n"
    "vec2 pow_2_complex(vec2 c) {\n"
    "  return vec2(c.x*c.x - c.y*c.y, 2.0 * c.x * c.y);\n"
    "}\n"
    "vec2 mandelbrot(vec2 c, vec2 c0) {\n"
    "  return pow_2_complex(c) + c0;\n"
    "}\n"
    "vec4 iterate_pixel(vec2 position) {\n"
    "  vec2 c = vec2(0);\n"
    "  for (int i=0; i < 100; i++) {\n"
    "    if (c.x*c.x + c.y*c.y > 2.0*2.0)\n"
    "      return i_to_rgb(i);\n"
    "    c = mandelbrot(c, position);\n"
    "  }\n"
    "  return vec4(0, 0, 0, 1);\n"
    "}\n"
    "void main() {\n"
    "  gl_FragColor = iterate_pixel(fractal_position);\n"
    "}";
/* *INDENT-ON* */

static gboolean
_src_mandelbrot_init (gpointer impl, GstGLContext * context,
    GstVideoInfo * v_info)
{
  struct SrcShader *src = impl;
  GError *error = NULL;

  src->base.context = context;

  if (src->shader)
    gst_object_unref (src->shader);
  src->shader = gst_gl_shader_new_link_with_stages (context, &error,
      gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          mandelbrot_vertex_src),
      gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          mandelbrot_fragment_src), NULL);
  if (!src->shader) {
    GST_ERROR_OBJECT (src->base.src, "%s", error->message);
    return FALSE;
  }

  src->attr_position =
      gst_gl_shader_get_attribute_location (src->shader, "position");
  if (src->attr_position == -1) {
    GST_ERROR_OBJECT (src->base.src, "No position attribute");
    return FALSE;
  }
  src->vertices = positions;
  src->n_vertices = 4;
  src->indices = indices_quad;
  src->n_indices = 6;

  gst_gl_shader_use (src->shader);
  gst_gl_shader_set_uniform_1f (src->shader, "aspect_ratio",
      (gfloat) GST_VIDEO_INFO_WIDTH (v_info) /
      (gfloat) GST_VIDEO_INFO_HEIGHT (v_info));
  gst_gl_context_clear_shader (src->base.context);

  return TRUE;
}

static gboolean
_src_mandelbrot_fill_bound_fbo (gpointer impl)
{
  struct SrcShader *src = impl;

  g_return_val_if_fail (src->base.context, FALSE);
  g_return_val_if_fail (src->shader, FALSE);

  gst_gl_shader_use (src->shader);
  gst_gl_shader_set_uniform_1f (src->shader, "time",
      (gfloat) src->base.src->running_time / GST_SECOND);

  return _src_shader_fill_bound_fbo (impl);
}

static void
_src_mandelbrot_free (gpointer impl)
{
  struct SrcShader *src = impl;

  if (!src)
    return;

  if (src->shader)
    gst_object_unref (src->shader);
  src->shader = NULL;

  g_free (impl);
}

static gpointer
_src_mandelbrot_new (GstGLTestSrc * test)
{
  struct SrcShader *src = g_new0 (struct SrcShader, 1);

  src->base.src = test;

  return src;
}

static const struct SrcFuncs src_mandelbrot = {
  GST_GL_TEST_SRC_MANDELBROT,
  _src_mandelbrot_new,
  _src_mandelbrot_init,
  _src_mandelbrot_fill_bound_fbo,
  _src_mandelbrot_free,
};

/* *INDENT-OFF* */
static const gchar *circular_vertex_src =
    "attribute vec4 position;\n"
    "varying vec2 uv;\n"
    "void main()\n"
    "{\n"
    "  gl_Position = position;\n"
    "  uv = position.xy;\n"
    "}";

static const gchar *circular_fragment_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform float aspect_ratio;\n"
    "varying vec2 uv;\n"
    "#define PI 3.14159265\n"
    "void main() {\n"
    "  float dist = 0.5 * sqrt(uv.x * uv.x + uv.y / aspect_ratio * uv.y / aspect_ratio);\n"
    "  float seg = floor(dist * 16.0);\n"
    "  if (seg <= 0.0 || seg >= 8.0) {\n"
    "    gl_FragColor = vec4(vec3(0.0), 1.0);\n"
    "  } else {\n"
    "    float d = floor (256.0 * dist * 200.0 * pow (2.0, - (seg - 1.0) / 4.0) + 0.5) / 128.0;\n"
    "    gl_FragColor = vec4 (vec3(sin (d * PI) * 0.5 + 0.5), 1.0);\n"
    "  }\n"
    "}";
/* *INDENT-ON* */

static gboolean
_src_circular_init (gpointer impl, GstGLContext * context,
    GstVideoInfo * v_info)
{
  struct SrcShader *src = impl;
  GError *error = NULL;

  src->base.context = context;

  if (src->shader)
    gst_object_unref (src->shader);
  src->shader = gst_gl_shader_new_link_with_stages (context, &error,
      gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          circular_vertex_src),
      gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
          GST_GLSL_VERSION_NONE,
          GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
          circular_fragment_src), NULL);
  if (!src->shader) {
    GST_ERROR_OBJECT (src->base.src, "%s", error->message);
    return FALSE;
  }

  src->attr_position =
      gst_gl_shader_get_attribute_location (src->shader, "position");
  if (src->attr_position == -1) {
    GST_ERROR_OBJECT (src->base.src, "No position attribute");
    return FALSE;
  }
  src->vertices = positions;
  src->n_vertices = 4;
  src->indices = indices_quad;
  src->n_indices = 6;

  gst_gl_shader_use (src->shader);
  gst_gl_shader_set_uniform_1f (src->shader, "aspect_ratio",
      (gfloat) GST_VIDEO_INFO_WIDTH (v_info) /
      (gfloat) GST_VIDEO_INFO_HEIGHT (v_info));
  gst_gl_context_clear_shader (src->base.context);

  return TRUE;
}

static void
_src_circular_free (gpointer impl)
{
  struct SrcShader *src = impl;

  if (!src)
    return;

  if (src->shader)
    gst_object_unref (src->shader);
  src->shader = NULL;

  g_free (impl);
}

static gpointer
_src_circular_new (GstGLTestSrc * test)
{
  struct SrcShader *src = g_new0 (struct SrcShader, 1);

  src->base.src = test;

  return src;
}

static const struct SrcFuncs src_circular = {
  GST_GL_TEST_SRC_CIRCULAR,
  _src_circular_new,
  _src_circular_init,
  _src_mandelbrot_fill_bound_fbo,
  _src_circular_free,
};

static const struct SrcFuncs *src_impls[] = {
  &src_smpte,
  &src_snow,
  &src_black,
  &src_white,
  &src_red,
  &src_green,
  &src_blue,
  &src_checkers1,
  &src_checkers2,
  &src_checkers4,
  &src_checkers8,
  &src_circular,
  &src_blink,
  &src_mandelbrot,
};

const struct SrcFuncs *
gst_gl_test_src_get_src_funcs_for_pattern (GstGLTestSrcPattern pattern)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (src_impls); i++) {
    if (src_impls[i]->pattern == pattern)
      return src_impls[i];
  }

  return NULL;
}
