/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@mail.com>
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

/**
 * SECTION:element-deinterlace
 *
 * Deinterlacing using based on fragment shaders.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch videotestsrc ! glupload ! gldeinterlace ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgldeinterlace.h"

#define GST_CAT_DEFAULT gst_gl_deinterlace_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_deinterlace_debug, "gldeinterlace", 0, "gldeinterlace element");

G_DEFINE_TYPE_WITH_CODE (GstGLDeinterlace, gst_gl_deinterlace,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_deinterlace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_deinterlace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_gl_deinterlace_reset (GstGLFilter * filter);
static gboolean gst_gl_deinterlace_init_shader (GstGLFilter * filter);
static gboolean gst_gl_deinterlace_filter (GstGLFilter * filter,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_deinterlace_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);
static void gst_gl_deinterlace_callback (gint width, gint height,
    guint texture, gpointer stuff);

/* *INDENT-OFF* */
static const gchar *greedyh_fragment_source =
  "uniform sampler2D tex;\n"
  "uniform sampler2D tex_prev;\n"
  "uniform float max_comb;\n"
  "uniform float motion_threshold;\n"
  "uniform float motion_sense;\n"
  "uniform float width;\n"
  "uniform float height;\n"

  "void main () {\n"
  "  vec2 texcoord = gl_TexCoord[0].xy;\n"
  "  if (int(mod(texcoord.y * height, 2.0)) == 0) {\n"
  "    gl_FragColor = vec4(texture2D(tex_prev, texcoord).rgb, 1.0);\n"
  "  } else {\n"
  "    vec2 texcoord_L1_a1, texcoord_L3_a1, texcoord_L1, texcoord_L3, texcoord_L1_1, texcoord_L3_1;\n"
  "    vec3 L1_a1, L3_a1, L1, L3, L1_1, L3_1;\n"

  "    texcoord_L1 = vec2(texcoord.x, texcoord.y - 1.0 / height);\n"
  "    texcoord_L3 = vec2(texcoord.x, texcoord.y + 1.0 / height);\n"
  "    L1 = texture2D(tex_prev, texcoord_L1).rgb;\n"
  "    L3 = texture2D(tex_prev, texcoord_L3).rgb;\n"
  "    if (texcoord.x == 1.0 && texcoord.y == 1.0) {\n"
  "      L1_1 = L1;\n"
  "      L3_1 = L3;\n"
  "    } else {\n"
  "      texcoord_L1_1 = vec2(texcoord.x + 1.0 / width, texcoord.y - 1.0 / height);\n"
  "      texcoord_L3_1 = vec2(texcoord.x + 1.0 / width, texcoord.y + 1.0 / height);\n"
  "      L1_1 = texture2D(tex_prev, texcoord_L1_1).rgb;\n"
  "      L3_1 = texture2D(tex_prev, texcoord_L3_1).rgb;\n"
  "    }\n"

  "    if (int(ceil(texcoord.x + texcoord.y)) == 0) {\n"
  "      L1_a1 = L1;\n"
  "      L3_a1 = L3;\n"
  "    } else {\n"
  "      texcoord_L1_a1 = vec2(texcoord.x - 1.0 / width, texcoord.y - 1.0 / height);\n"
  "      texcoord_L3_a1 = vec2(texcoord.x - 1.0 / width, texcoord.y + 1.0 / height);\n"
  "      L1_a1 = texture2D(tex_prev, texcoord_L1_a1).rgb;\n"
  "      L3_a1 = texture2D(tex_prev, texcoord_L3_a1).rgb;\n"
  "    }\n"
          //STEP 1
  "    vec3 avg_a1 = (L1_a1 + L3_a1) / 2.0;\n"
  "    vec3 avg = (L1 + L3) / 2.0;\n"
  "    vec3 avg_1 = (L1_1 + L3_1) / 2.0;\n"
  "    vec3 avg_s = (avg_a1 + avg_1) / 2.0;\n"
  "    vec3 avg_sc = (avg_s + avg) / 2.0;\n"
  "    vec3 L2 = texture2D(tex, texcoord).rgb;\n"
  "    vec3 LP2 = texture2D(tex_prev, texcoord).rgb;\n"
  "    vec3 best;\n"
  "    if (abs(L2.r - avg_sc.r) < abs(LP2.r - avg_sc.r)) {\n"
  "      best.r = L2.r;\n" "    } else {\n"
  "      best.r = LP2.r;\n"
  "    }\n"

  "    if (abs(L2.g - avg_sc.g) < abs(LP2.g - avg_sc.g)) {\n"
  "      best.g = L2.g;\n"
  "    } else {\n"
  "      best.g = LP2.g;\n"
  "    }\n"

  "    if (abs(L2.b - avg_sc.b) < abs(LP2.b - avg_sc.b)) {\n"
  "      best.b = L2.b;\n"
  "    } else {\n"
  "      best.b = LP2.b;\n"
  "    }\n"
          //STEP 2
  "    vec3 last;\n"
  "    last.r = clamp(best.r, max(min(L1.r, L3.r) - max_comb, 0.0), min(max(L1.r, L3.r) + max_comb, 1.0));\n"
  "    last.g = clamp(best.g, max(min(L1.g, L3.g) - max_comb, 0.0), min(max(L1.g, L3.g) + max_comb, 1.0));\n"
  "    last.b = clamp(best.b, max(min(L1.b, L3.b) - max_comb, 0.0), min(max(L1.b, L3.b) + max_comb, 1.0));\n"
          //STEP 3
  "    const vec3 luma = vec3 (0.299011, 0.586987, 0.114001);"
  "    float mov = min(max(abs(dot(L2 - LP2, luma)) - motion_threshold, 0.0) * motion_sense, 1.0);\n"
  "    last = last * (1.0 - mov) + avg_sc * mov;\n"
  "    gl_FragColor = vec4(last, 1.0);\n"
  "  }\n"
  "}\n";
/* *INDENT-ON* */

static void
gst_gl_deinterlace_class_init (GstGLDeinterlaceClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_deinterlace_set_property;
  gobject_class->get_property = gst_gl_deinterlace_get_property;

  gst_element_class_set_metadata (element_class,
      "OpenGL deinterlacing filter", "Deinterlace",
      "Deinterlacing based on fragment shaders",
      "Julien Isorce <julien.isorce@mail.com>");

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_deinterlace_filter;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_deinterlace_filter_texture;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_deinterlace_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_deinterlace_reset;
}

static void
gst_gl_deinterlace_init (GstGLDeinterlace * filter)
{
  filter->shader = NULL;
  filter->prev_buffer = NULL;
  filter->prev_tex = 0;
}

static void
gst_gl_deinterlace_reset (GstGLFilter * filter)
{
  GstGLDeinterlace *deinterlace_filter = GST_GL_DEINTERLACE (filter);

  if (deinterlace_filter->prev_buffer) {
    gst_buffer_unref (deinterlace_filter->prev_buffer);
    deinterlace_filter->prev_buffer = NULL;
  }
  //blocking call, wait the opengl thread has destroyed the shader
  if (deinterlace_filter->shader)
    gst_gl_context_del_shader (filter->context, deinterlace_filter->shader);
  deinterlace_filter->shader = NULL;
}

static void
gst_gl_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLDeinterlace *filter = GST_GL_DEINTERLACE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_deinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLDeinterlace *filter = GST_GL_DEINTERLACE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_deinterlace_init_shader (GstGLFilter * filter)
{
  GstGLDeinterlace *deinterlace_filter = GST_GL_DEINTERLACE (filter);

  //blocking call, wait the opengl thread has compiled the shader
  return gst_gl_context_gen_shader (filter->context, 0, greedyh_fragment_source,
      &deinterlace_filter->shader);
}

static gboolean
gst_gl_deinterlace_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLDeinterlace *deinterlace_filter = GST_GL_DEINTERLACE (filter);

  //blocking call, use a FBO
  gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
      gst_gl_deinterlace_callback, deinterlace_filter);

  return TRUE;
}

static gboolean
gst_gl_deinterlace_filter (GstGLFilter * filter, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLDeinterlace *deinterlace_filter = GST_GL_DEINTERLACE (filter);

  gst_gl_filter_filter_texture (filter, inbuf, outbuf);

  if (deinterlace_filter->prev_buffer) {
    gst_buffer_unref (deinterlace_filter->prev_buffer);
  }
  deinterlace_filter->prev_buffer = gst_buffer_ref (inbuf);

  return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_deinterlace_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLDeinterlace *deinterlace_filter = GST_GL_DEINTERLACE (stuff);
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->context->gl_vtable;
  guint temp;

  GLfloat verts[] = { -1.0, -1.0,
    1.0, -1.0,
    1.0, 1.0,
    -1.0, 1.0
  };
  GLfloat texcoords0[] = { 0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };
  GLfloat texcoords1[] = { 0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (deinterlace_filter->shader);

  gl->Enable (GL_TEXTURE_2D);

  if (G_UNLIKELY (deinterlace_filter->prev_tex == 0)) {
    gst_gl_context_gen_texture (filter->context,
        &deinterlace_filter->prev_tex,
        GST_VIDEO_INFO_FORMAT (&filter->out_info),
        GST_VIDEO_INFO_WIDTH (&filter->out_info),
        GST_VIDEO_INFO_HEIGHT (&filter->out_info));
  } else {
    gl->ActiveTexture (GL_TEXTURE1);
    gst_gl_shader_set_uniform_1i (deinterlace_filter->shader, "tex_prev", 1);
    gl->BindTexture (GL_TEXTURE_2D, deinterlace_filter->prev_tex);
  }

  gl->ActiveTexture (GL_TEXTURE0);
  gst_gl_shader_set_uniform_1i (deinterlace_filter->shader, "tex", 0);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1f (deinterlace_filter->shader, "max_comb",
      5.0f / 255.0f);
  gst_gl_shader_set_uniform_1f (deinterlace_filter->shader, "motion_threshold",
      25.0f / 255.0f);
  gst_gl_shader_set_uniform_1f (deinterlace_filter->shader, "motion_sense",
      30.0f / 255.0f);

  gst_gl_shader_set_uniform_1f (deinterlace_filter->shader, "width",
      GST_VIDEO_INFO_WIDTH (&filter->out_info));
  gst_gl_shader_set_uniform_1f (deinterlace_filter->shader, "height",
      GST_VIDEO_INFO_HEIGHT (&filter->out_info));

  gl->ClientActiveTexture (GL_TEXTURE0);

  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);
  gl->EnableClientState (GL_VERTEX_ARRAY);

  gl->VertexPointer (2, GL_FLOAT, 0, &verts);
  gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords0);

  gl->ClientActiveTexture (GL_TEXTURE1);
  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);
  gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords1);

  gl->DrawArrays (GL_TRIANGLE_FAN, 0, 4);

  gl->DisableClientState (GL_VERTEX_ARRAY);
  gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->ClientActiveTexture (GL_TEXTURE0);
  gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->Disable (GL_TEXTURE_2D);

  if (texture == filter->in_tex_id) {
    temp = filter->in_tex_id;
    filter->in_tex_id = deinterlace_filter->prev_tex;
    deinterlace_filter->prev_tex = temp;
  } else {
    deinterlace_filter->prev_tex = texture;
  }
}
