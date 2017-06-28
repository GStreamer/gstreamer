/*
 * GStreamer
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
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

#include <string.h>
#include <stdio.h>

#include "gl.h"
#include "gstglcolorconvert.h"
#include "gstglsl_private.h"

/**
 * SECTION:gstglcolorconvert
 * @title: GstGlColorConvert
 * @short_description: convert between video color spaces and formats
 * @see_also: #GstGLUpload, #GstGLMemory, #GstGLBaseMemory
 *
 * #GstGLColorConvert is an object that converts between color spaces and/or
 * formats using OpenGL Shaders.
 *
 * A #GstGLColorConvert can be created with gst_gl_color_convert_new(), the
 * configuration negotiated with gst_gl_color_convert_transform_caps() and the
 * conversion performed with gst_gl_color_convert_perform().
 *
 * The glcolorconvertelement provides a GStreamer element that uses
 * #GstGLColorConvert to convert between video formats and color spaces.
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

static void _do_convert (GstGLContext * context, GstGLColorConvert * convert);
static gboolean _init_convert (GstGLColorConvert * convert);
static gboolean _init_convert_fbo (GstGLColorConvert * convert);
static GstBuffer *_gst_gl_color_convert_perform_unlocked (GstGLColorConvert *
    convert, GstBuffer * inbuf);

static gboolean _do_convert_draw (GstGLContext * context,
    GstGLColorConvert * convert);

/* *INDENT-OFF* */

#define YUV_TO_RGB_COEFFICIENTS \
      "uniform vec3 offset;\n" \
      "uniform vec3 coeff1;\n" \
      "uniform vec3 coeff2;\n" \
      "uniform vec3 coeff3;\n"

/* FIXME: use the colormatrix support from videoconvert */

/* BT. 601 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_yuv_bt601_offset[] = {-0.0625f, -0.5f, -0.5f};
static const gfloat from_yuv_bt601_rcoeff[] = {1.164f, 0.000f, 1.596f};
static const gfloat from_yuv_bt601_gcoeff[] = {1.164f,-0.391f,-0.813f};
static const gfloat from_yuv_bt601_bcoeff[] = {1.164f, 2.018f, 0.000f};

/* BT. 709 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_yuv_bt709_offset[] = {-0.0625f, -0.5f, -0.5f};
static const gfloat from_yuv_bt709_rcoeff[] = {1.164f, 0.000f, 1.787f};
static const gfloat from_yuv_bt709_gcoeff[] = {1.164f,-0.213f,-0.531f};
static const gfloat from_yuv_bt709_bcoeff[] = {1.164f,2.112f, 0.000f};

#define RGB_TO_YUV_COEFFICIENTS \
      "uniform vec3 offset;\n" \
      "uniform vec3 coeff1;\n" \
      "uniform vec3 coeff2;\n" \
      "uniform vec3 coeff3;\n"

/* Matrix inverses of the color matrices found above */
/* BT. 601 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_rgb_bt601_offset[] = {0.0625f, 0.5f, 0.5f};
static const gfloat from_rgb_bt601_ycoeff[] = {0.256816f, 0.504154f, 0.0979137f};
static const gfloat from_rgb_bt601_ucoeff[] = {-0.148246f, -0.29102f, 0.439266f};
static const gfloat from_rgb_bt601_vcoeff[] = {0.439271f, -0.367833f, -0.071438f};

/* BT. 709 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_rgb_bt709_offset[] = {0.0625f, 0.5f, 0.5f};
static const gfloat from_rgb_bt709_ycoeff[] = {0.182604f, 0.614526f, 0.061976f};
static const gfloat from_rgb_bt709_ucoeff[] = {-0.100640f, -0.338688f, 0.439327f};
static const gfloat from_rgb_bt709_vcoeff[] = {0.440654f, -0.400285f, -0.040370f};

/* GRAY16 to RGB conversion
 *  data transfered as GL_LUMINANCE_ALPHA then convert back to GRAY16 
 *  high byte weight as : 255*256/65535 
 *  ([0~1] denormalize to [0~255],shift to high byte,normalize to [0~1])
 *  low byte weight as : 255/65535 (similar)
 * */
#define COMPOSE_WEIGHT \
    "const vec2 compose_weight = vec2(0.996109, 0.003891);\n"

#define DEFAULT_UNIFORMS         \
    "#ifdef GL_ES\n"             \
    "precision mediump float;\n" \
    "#endif\n"                   \
    "uniform vec2 tex_scale0;\n" \
    "uniform vec2 tex_scale1;\n" \
    "uniform vec2 tex_scale2;\n" \
    "uniform float width;\n"     \
    "uniform float height;\n"    \
    "uniform float poffset_x;\n" \
    "uniform float poffset_y;\n"

#define MAX_FUNCTIONS 4

#define glsl_OES_extension_string "#extension GL_OES_EGL_image_external : require \n"

struct shader_templ
{
  const gchar *extensions;
  const gchar *uniforms;
  const gchar *functions[MAX_FUNCTIONS];
  const gchar *body;

  GstGLTextureTarget target;
};

#define glsl_func_yuv_to_rgb \
    "vec3 yuv_to_rgb (vec3 val, vec3 offset, vec3 ycoeff, vec3 ucoeff, vec3 vcoeff) {\n" \
    "  vec3 rgb;\n"                 \
    "  val += offset;\n"            \
    "  rgb.r = dot(val, ycoeff);\n" \
    "  rgb.g = dot(val, ucoeff);\n" \
    "  rgb.b = dot(val, vcoeff);\n" \
    "  return rgb;\n"               \
    "}\n"

#define glsl_func_rgb_to_yuv \
    "vec3 rgb_to_yuv (vec3 val, vec3 offset, vec3 rcoeff, vec3 gcoeff, vec3 bcoeff) {\n" \
    "  vec3 yuv;\n"                         \
    "  yuv.r = dot(val.rgb, rcoeff);\n"     \
    "  yuv.g = dot(val.rgb, gcoeff);\n"     \
    "  yuv.b = dot(val.rgb, bcoeff);\n"     \
    "  yuv += offset;\n"                    \
    "  return yuv;\n"                       \
    "}\n"

/* Channel reordering for XYZ <-> ZYX conversion */
static const struct shader_templ templ_REORDER =
  { NULL,
    DEFAULT_UNIFORMS "uniform sampler2D tex;\n",
    { NULL, },
    "vec4 t = texture2D(tex, texcoord * tex_scale0);\n"
    "%s\n" /* clobber alpha channel? */
    "gl_FragColor = vec4(t.%c, t.%c, t.%c, t.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* GRAY16 to RGB conversion
 *  data transfered as GL_LUMINANCE_ALPHA then convert back to GRAY16 
 *  high byte weight as : 255*256/65535 
 *  ([0~1] denormalize to [0~255],shift to high byte,normalize to [0~1])
 *  low byte weight as : 255/65535 (similar)
 * */
static const struct shader_templ templ_COMPOSE =
  { NULL,
    DEFAULT_UNIFORMS COMPOSE_WEIGHT "uniform sampler2D tex;\n",
    { NULL, },
    "vec4 rgba;\n"
    "vec4 t = texture2D(tex, texcoord * tex_scale0);\n"
    "rgba.rgb = vec3 (dot(t.%c%c, compose_weight));"
    "rgba.a = 1.0;\n"
    "gl_FragColor = vec4(rgba.%c, rgba.%c, rgba.%c, rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_AYUV_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 texel, rgba;\n"
    "texel = texture2D(tex, texcoord * tex_scale0);\n"
    "rgba.rgb = yuv_to_rgb (texel.yzw, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = texel.r;\n"
    "gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_RGB_to_AYUV =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel, ayuv;\n"
    "texel = texture2D(tex, texcoord).%c%c%c%c;\n"
    "ayuv.yzw = rgb_to_yuv (texel.rgb, offset, coeff1, coeff2, coeff3);\n"
    "ayuv.x = %s;\n"
    "gl_FragColor = ayuv;\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* YUV to RGB conversion */
static const struct shader_templ templ_PLANAR_YUV_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D Ytex, Utex, Vtex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 texel, rgba;\n"
    /* FIXME: should get the sampling right... */
    "texel.x = texture2D(Ytex, texcoord * tex_scale0).r;\n"
    "texel.y = texture2D(Utex, texcoord * tex_scale1).r;\n"
    "texel.z = texture2D(Vtex, texcoord * tex_scale2).r;\n"
    "rgba.rgb = yuv_to_rgb (texel.xyz, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = 1.0;\n"
    "gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_RGB_to_PLANAR_YUV =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n"
    "uniform vec2 chroma_sampling;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel;\n"
    "vec3 yuv;\n"
    "texel = texture2D(tex, texcoord).%c%c%c%c;\n"
    /* FIXME: this is not quite correct yet */
    "vec4 uv_texel = vec4(0.0);\n"
    /* One u and v sample can be generated by a nxm sized block given by
     * @chroma_sampling.  The result is the average of all the values in the
     * block computed with a rolling average.
     */
    "vec2 unnormalization;\n"
    "if (texcoord.x == v_texcoord.x) {\n"
    "  unnormalization = vec2(width, height);\n"
    "} else {\n"
    "  unnormalization = vec2 (1.0);\n"
    "}\n"
     /* scale for chroma size */
    "vec2 chroma_pos = texcoord * chroma_sampling * unnormalization;\n"
     /* offset chroma to the center of the first texel in the block */
    "chroma_pos -= clamp(chroma_sampling * 0.5 - 0.5, vec2(0.0), chroma_sampling);\n"
    "if (chroma_pos.x < width && chroma_pos.y < height) {\n"
    "  for (int i = 0; i < int(chroma_sampling.x); i++) {\n"
    "    vec2 delta = vec2 (float(i), 0.0);\n"
    "    for (int j = 0; j < int(chroma_sampling.y); j++) {\n"
    "      int n = (i+1)*(j+1);\n"
    "      delta.y = float(j);\n"
    "      vec4 s = texture2D(tex, (chroma_pos + delta) / unnormalization).%c%c%c%c;\n"
           /* rolling average */
    "      uv_texel = (float(n-1) * uv_texel + s) / float(n);\n"
    "    }\n"
    "  }\n"
    "}\n"
    "yuv.x = rgb_to_yuv (texel.rgb, offset, coeff1, coeff2, coeff3).x;\n"
    "yuv.yz = rgb_to_yuv (uv_texel.rgb, offset, coeff1, coeff2, coeff3).yz;\n"
    "gl_FragData[0] = vec4(yuv.x, 0.0, 0.0, 1.0);\n"
    "gl_FragData[1] = vec4(yuv.y, 0.0, 0.0, 1.0);\n"
    "gl_FragData[2] = vec4(yuv.z, 0.0, 0.0, 1.0);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* NV12/NV21 to RGB conversion */
static const struct shader_templ templ_NV12_NV21_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D Ytex, UVtex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 rgba;\n"
    "vec3 yuv;\n"
    /* FIXME: should get the sampling right... */
    "yuv.x=texture2D(Ytex, texcoord * tex_scale0).r;\n"
    "yuv.yz=texture2D(UVtex, texcoord * tex_scale1).%c%c;\n"
    "rgba.rgb = yuv_to_rgb (yuv, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = 1.0;\n"
    "gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* RGB to NV12/NV21 conversion */
/* NV12: u, v
   NV21: v, u */
static const struct shader_templ templ_RGB_to_NV12_NV21 =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel, uv_texel;\n"
    "vec3 yuv;\n"
    "texel = texture2D(tex, texcoord).%c%c%c%c;\n"
    "uv_texel = texture2D(tex, texcoord * tex_scale0 * 2.0).%c%c%c%c;\n"
    "yuv.x = rgb_to_yuv (texel.rgb, offset, coeff1, coeff2, coeff3).x;\n"
    "yuv.yz = rgb_to_yuv (uv_texel.rgb, offset, coeff1, coeff2, coeff3).yz;\n"
    "gl_FragData[0] = vec4(yuv.x, 0.0, 0.0, 1.0);\n"
    "gl_FragData[1] = vec4(yuv.%c, yuv.%c, 0.0, 1.0);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* YUY2:r,g,a
   UYVY:a,b,r */
static const struct shader_templ templ_YUY2_UYVY_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D Ytex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 rgba, uv_texel;\n"
    "vec3 yuv;\n"
    /* FIXME: should get the sampling right... */
    "float dx1 = -poffset_x;\n"
    "float dx2 = 0.0;\n"
    "yuv.x = texture2D(Ytex, texcoord * tex_scale0).%c;\n"
    /* v_texcoord are normalized, texcoord may not be e.g. rectangle textures */
    "float inorder = mod (v_texcoord.x * width, 2.0);\n"
    "if (inorder < 1.0) {\n"
    "  dx2 = -dx1;\n"
    "  dx1 = 0.0;\n"
    "}\n"
    "uv_texel.rg = texture2D(Ytex, texcoord * tex_scale0 + vec2(dx1, 0.0)).r%c;\n"
    "uv_texel.ba = texture2D(Ytex, texcoord * tex_scale0 + vec2(dx2, 0.0)).r%c;\n"
    "yuv.yz = uv_texel.%c%c;\n"
    "rgba.rgb = yuv_to_rgb (yuv, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = 1.0;\n"
    "gl_FragColor = vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_RGB_to_YUY2_UYVY =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel1, texel2;\n"
    "vec3 yuv, yuv1, yuv2;\n"
    "float fx, dx, fy;\n"
    /* v_texcoord are normalized, texcoord may not be e.g. rectangle textures */
    "float inorder = mod (v_texcoord.x * width, 2.0);\n"
    "fx = texcoord.x;\n"
    "dx = poffset_x;\n"
    "if (inorder > 1.0) {\n"
    "  dx = -dx;\n"
    "}\n"
    "fy = texcoord.y;\n"
    "texel1 = texture2D(tex, vec2(fx, fy)).%c%c%c%c;\n"
    "texel2 = texture2D(tex, vec2(fx + dx, fy)).%c%c%c%c;\n"
    "yuv1 = rgb_to_yuv (texel1.rgb, offset, coeff1, coeff2, coeff3);\n"
    "yuv2 = rgb_to_yuv (texel2.rgb, offset, coeff1, coeff2, coeff3);\n"
    "yuv.x = yuv1.x;\n"
    "yuv.yz = (yuv1.yz + yuv2.yz) * 0.5;\n"
    "if (inorder < 1.0) {\n"
    "  gl_FragColor = vec4(yuv.%c, yuv.%c, 0.0, 0.0);\n"
    "} else {\n"
    "  gl_FragColor = vec4(yuv.%c, yuv.%c, 0.0, 0.0);\n"
    "}\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const gchar text_vertex_shader[] =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texcoord;   \n"
    "varying vec2 v_texcoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "  gl_Position = a_position; \n"
    "  v_texcoord = a_texcoord;  \n"
    "}                            \n";

static const GLfloat vertices[] = {
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f
};

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

/* *INDENT-ON* */

struct ConvertInfo
{
  gint in_n_textures;
  gint out_n_textures;
  const struct shader_templ *templ;
  gchar *frag_body;
  gchar *frag_prog;
  const gchar *shader_tex_names[GST_VIDEO_MAX_PLANES];
  gfloat *cms_offset;
  gfloat *cms_coeff1;           /* r,y */
  gfloat *cms_coeff2;           /* g,u */
  gfloat *cms_coeff3;           /* b,v */
  gfloat chroma_sampling[2];
};

struct _GstGLColorConvertPrivate
{
  gboolean result;

  struct ConvertInfo convert_info;

  GstGLTextureTarget from_texture_target;
  GstGLTextureTarget to_texture_target;

  GstGLMemory *in_tex[GST_VIDEO_MAX_PLANES];
  GstGLMemory *out_tex[GST_VIDEO_MAX_PLANES];

  GstGLFormat in_tex_formats[GST_VIDEO_MAX_PLANES];

  GLuint vao;
  GLuint vertex_buffer;
  GLuint vbo_indices;
  GLuint attr_position;
  GLuint attr_texture;

  GstCaps *in_caps;
  GstCaps *out_caps;

  GstBufferPool *pool;
  gboolean pool_started;
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_color_convert_debug);
#define GST_CAT_DEFAULT gst_gl_color_convert_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_color_convert_debug, "glconvert", 0, "convert");

G_DEFINE_TYPE_WITH_CODE (GstGLColorConvert, gst_gl_color_convert,
    GST_TYPE_OBJECT, DEBUG_INIT);
static void gst_gl_color_convert_finalize (GObject * object);
static void gst_gl_color_convert_reset (GstGLColorConvert * convert);

#define GST_GL_COLOR_CONVERT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_COLOR_CONVERT, GstGLColorConvertPrivate))

static void
gst_gl_color_convert_class_init (GstGLColorConvertClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLColorConvertPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_color_convert_finalize;
}

static void
gst_gl_color_convert_init (GstGLColorConvert * convert)
{
  convert->priv = GST_GL_COLOR_CONVERT_GET_PRIVATE (convert);

  gst_gl_color_convert_reset (convert);
}

/**
 * gst_gl_color_convert_new:
 * @context: a #GstGLContext
 *
 * Returns: a new #GstGLColorConvert object
 *
 * Since: 1.4
 */
GstGLColorConvert *
gst_gl_color_convert_new (GstGLContext * context)
{
  GstGLColorConvert *convert;

  convert = g_object_new (GST_TYPE_GL_COLOR_CONVERT, NULL);

  convert->context = gst_object_ref (context);

  gst_video_info_set_format (&convert->in_info, GST_VIDEO_FORMAT_ENCODED, 0, 0);
  gst_video_info_set_format (&convert->out_info, GST_VIDEO_FORMAT_ENCODED, 0,
      0);

  GST_DEBUG_OBJECT (convert,
      "Created new colorconvert for context %" GST_PTR_FORMAT, context);

  return convert;
}

static void
gst_gl_color_convert_finalize (GObject * object)
{
  GstGLColorConvert *convert;

  convert = GST_GL_COLOR_CONVERT (object);

  gst_gl_color_convert_reset (convert);

  if (convert->context) {
    gst_object_unref (convert->context);
    convert->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_color_convert_parent_class)->finalize (object);
}

static void
_reset_gl (GstGLContext * context, GstGLColorConvert * convert)
{
  const GstGLFuncs *gl = context->gl_vtable;

  if (convert->priv->vao) {
    gl->DeleteVertexArrays (1, &convert->priv->vao);
    convert->priv->vao = 0;
  }

  if (convert->priv->vertex_buffer) {
    gl->DeleteBuffers (1, &convert->priv->vertex_buffer);
    convert->priv->vertex_buffer = 0;
  }

  if (convert->priv->vbo_indices) {
    gl->DeleteBuffers (1, &convert->priv->vbo_indices);
    convert->priv->vbo_indices = 0;
  }
}

static void
gst_gl_color_convert_reset_shader (GstGLColorConvert * convert)
{
  convert->priv->convert_info.chroma_sampling[0] = 1.0f;
  convert->priv->convert_info.chroma_sampling[1] = 1.0f;

  if (convert->priv->convert_info.frag_prog) {
    g_free (convert->priv->convert_info.frag_prog);
    convert->priv->convert_info.frag_prog = NULL;
  }
  if (convert->priv->convert_info.frag_body) {
    g_free (convert->priv->convert_info.frag_body);
    convert->priv->convert_info.frag_body = NULL;
  }
  if (convert->shader) {
    gst_object_unref (convert->shader);
    convert->shader = NULL;
  }

  convert->initted = FALSE;
}

static void
gst_gl_color_convert_reset (GstGLColorConvert * convert)
{
  guint i;

  if (convert->fbo) {
    gst_object_unref (convert->fbo);
    convert->fbo = NULL;
  }

  for (i = 0; i < convert->priv->convert_info.out_n_textures; i++) {
    if (convert->priv->out_tex[i])
      gst_memory_unref ((GstMemory *) convert->priv->out_tex[i]);
    convert->priv->out_tex[i] = NULL;
  }

  if (convert->priv->pool) {
    convert->priv->pool_started = FALSE;

    gst_object_unref (convert->priv->pool);
    convert->priv->pool = NULL;
  }

  gst_caps_replace (&convert->priv->in_caps, NULL);
  gst_caps_replace (&convert->priv->out_caps, NULL);

  if (convert->context) {
    gst_gl_context_thread_add (convert->context,
        (GstGLContextThreadFunc) _reset_gl, convert);
  }

  gst_gl_color_convert_reset_shader (convert);
}

static gboolean
_gst_gl_color_convert_can_passthrough_info (GstVideoInfo * in,
    GstVideoInfo * out)
{
  gint i;

  if (GST_VIDEO_INFO_FORMAT (in) != GST_VIDEO_INFO_FORMAT (out))
    return FALSE;
  if (GST_VIDEO_INFO_WIDTH (in) != GST_VIDEO_INFO_WIDTH (out))
    return FALSE;
  if (GST_VIDEO_INFO_HEIGHT (in) != GST_VIDEO_INFO_HEIGHT (out))
    return FALSE;
  if (GST_VIDEO_INFO_SIZE (in) != GST_VIDEO_INFO_SIZE (out))
    return FALSE;

  for (i = 0; i < in->finfo->n_planes; i++) {
    if (in->stride[i] != out->stride[i])
      return FALSE;
    if (in->offset[i] != out->offset[i])
      return FALSE;
  }

  if (!gst_video_colorimetry_is_equal (&in->colorimetry, &out->colorimetry))
    return FALSE;
  if (in->chroma_site != out->chroma_site)
    return FALSE;

  return TRUE;
}

static gboolean
_gst_gl_color_convert_set_caps_unlocked (GstGLColorConvert * convert,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstVideoInfo in_info, out_info;
  GstCapsFeatures *in_features, *out_features;
  GstGLTextureTarget from_target, to_target;
  gboolean passthrough;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (in_caps, FALSE);
  g_return_val_if_fail (out_caps, FALSE);

  GST_LOG_OBJECT (convert, "Setting caps in %" GST_PTR_FORMAT
      " out %" GST_PTR_FORMAT, in_caps, out_caps);

  if (!gst_video_info_from_caps (&in_info, in_caps))
    g_assert_not_reached ();

  if (!gst_video_info_from_caps (&out_info, out_caps))
    g_assert_not_reached ();

  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&in_info) !=
      GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&in_info) !=
      GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&out_info) !=
      GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&out_info) !=
      GST_VIDEO_FORMAT_ENCODED, FALSE);

  in_features = gst_caps_get_features (in_caps, 0);
  out_features = gst_caps_get_features (out_caps, 0);

  if (!gst_caps_features_contains (in_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY)
      || !gst_caps_features_contains (out_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    return FALSE;
  }

  {
    GstStructure *in_s = gst_caps_get_structure (in_caps, 0);
    GstStructure *out_s = gst_caps_get_structure (out_caps, 0);

    if (gst_structure_has_field_typed (in_s, "texture-target", G_TYPE_STRING))
      from_target =
          gst_gl_texture_target_from_string (gst_structure_get_string (in_s,
              "texture-target"));
    else
      from_target = GST_GL_TEXTURE_TARGET_2D;

    if (gst_structure_has_field_typed (out_s, "texture-target", G_TYPE_STRING))
      to_target =
          gst_gl_texture_target_from_string (gst_structure_get_string (out_s,
              "texture-target"));
    else
      to_target = GST_GL_TEXTURE_TARGET_2D;

    if (to_target == GST_GL_TEXTURE_TARGET_NONE
        || from_target == GST_GL_TEXTURE_TARGET_NONE)
      /* invalid caps */
      return FALSE;
  }

  if (gst_video_info_is_equal (&convert->in_info, &in_info) &&
      gst_video_info_is_equal (&convert->out_info, &out_info) &&
      convert->priv->from_texture_target == from_target &&
      convert->priv->to_texture_target == to_target)
    return TRUE;

  /* If input and output are identical, pass through directly */
  passthrough =
      _gst_gl_color_convert_can_passthrough_info (&in_info, &out_info) &&
      from_target == to_target;

  if (!passthrough && to_target != GST_GL_TEXTURE_TARGET_2D
      && to_target != GST_GL_TEXTURE_TARGET_RECTANGLE)
    return FALSE;

  {
    guint yuv_gray_flags, in_flags, out_flags;

    in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info.finfo);
    out_flags = GST_VIDEO_FORMAT_INFO_FLAGS (out_info.finfo);
    yuv_gray_flags = GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_GRAY;

    /* GRAY/YUV -> GRAY/YUV is not supported for non-passthrough */
    if (!passthrough && (in_flags & yuv_gray_flags) != 0
        && (out_flags & yuv_gray_flags) != 0)
      return FALSE;
  }

  gst_gl_color_convert_reset (convert);
  convert->in_info = in_info;
  convert->out_info = out_info;
  gst_caps_replace (&convert->priv->in_caps, in_caps);
  gst_caps_replace (&convert->priv->out_caps, out_caps);
  convert->priv->from_texture_target = from_target;
  convert->priv->to_texture_target = to_target;
  convert->initted = FALSE;

  convert->passthrough = passthrough;
#ifndef GST_DISABLE_GST_DEBUG
  if (G_UNLIKELY (convert->passthrough))
    GST_DEBUG_OBJECT (convert,
        "Configuring passthrough mode for same in/out caps");
  else {
    GST_DEBUG_OBJECT (convert, "Color converting %" GST_PTR_FORMAT
        " to %" GST_PTR_FORMAT, in_caps, out_caps);
  }
#endif

  return TRUE;
}

/**
 * gst_gl_color_convert_set_caps:
 * @convert: a #GstGLColorConvert
 * @in_caps: input #GstCaps
 * @out_caps: output #GstCaps
 *
 * Initializes @convert with the information required for conversion.
 *
 * Since: 1.6
 */
gboolean
gst_gl_color_convert_set_caps (GstGLColorConvert * convert,
    GstCaps * in_caps, GstCaps * out_caps)
{
  gboolean ret;

  GST_OBJECT_LOCK (convert);
  ret = _gst_gl_color_convert_set_caps_unlocked (convert, in_caps, out_caps);
  GST_OBJECT_UNLOCK (convert);

  return ret;
}

/**
 * gst_gl_color_convert_decide_allocation:
 * @convert: a #GstGLColorConvert
 * @query: a completed ALLOCATION #GstQuery
 *
 * Provides an implementation of #GstBaseTransfromClass::decide_allocation()
 *
 * Returns: whether the allocation parameters were successfully chosen
 *
 * Since: 1.8
 */
gboolean
gst_gl_color_convert_decide_allocation (GstGLColorConvert * convert,
    GstQuery * query)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size, n, i;
  gboolean update_pool;
  GstGLVideoAllocationParams *params;
  GstVideoInfo vinfo;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    return FALSE;

  gst_video_info_from_caps (&vinfo, caps);

  n = gst_query_get_n_allocation_pools (query);
  if (n > 0) {
    update_pool = TRUE;
    for (i = 0; i < n; i++) {
      gst_query_parse_nth_allocation_pool (query, i, &pool, &size, &min, &max);

      if (!pool || !GST_IS_GL_BUFFER_POOL (pool)) {
        if (pool)
          gst_object_unref (pool);
        pool = NULL;
      }
    }
  }

  if (!pool) {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool)
    pool = gst_gl_buffer_pool_new (convert->context);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (gst_query_find_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, NULL))
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_GL_SYNC_META);

  params = gst_gl_video_allocation_params_new (convert->context, NULL, &vinfo,
      0, NULL, convert->priv->to_texture_target, 0);
  gst_buffer_pool_config_set_gl_allocation_params (config,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);

  if (!gst_buffer_pool_set_config (pool, config))
    GST_WARNING_OBJECT (convert, "Failed to set buffer pool config");

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (convert->priv->pool) {
    gst_object_unref (convert->priv->pool);
    convert->priv->pool_started = FALSE;
  }
  convert->priv->pool = pool;

  return TRUE;
}

static void
_init_value_string_list (GValue * list, ...)
{
  GValue item = G_VALUE_INIT;
  gchar *str;
  va_list args;

  g_value_init (list, GST_TYPE_LIST);

  va_start (args, list);
  while ((str = va_arg (args, gchar *))) {
    g_value_init (&item, G_TYPE_STRING);
    g_value_set_string (&item, str);

    gst_value_list_append_value (list, &item);
    g_value_unset (&item);
  }
  va_end (args);
}

/* copies the given caps */
static GstCaps *
gst_gl_color_convert_caps_transform_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GValue rgb_formats = G_VALUE_INIT;

  _init_value_string_list (&rgb_formats, "RGBA", "ARGB", "BGRA", "ABGR", "RGBx",
      "xRGB", "BGRx", "xBGR", "RGB", "BGR", NULL);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    const GValue *format;

    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    format = gst_structure_get_value (st, "format");
    st = gst_structure_copy (st);
    if (GST_VALUE_HOLDS_LIST (format)) {
      gboolean have_rgb_formats = FALSE;
      GValue passthrough_formats = G_VALUE_INIT;
      gint j, len;

      g_value_init (&passthrough_formats, GST_TYPE_LIST);
      len = gst_value_list_get_size (format);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          const gchar *format_str = g_value_get_string (val);
          GstVideoFormat v_format = gst_video_format_from_string (format_str);
          const GstVideoFormatInfo *t_info =
              gst_video_format_get_info (v_format);
          if (GST_VIDEO_FORMAT_INFO_FLAGS (t_info) & (GST_VIDEO_FORMAT_FLAG_YUV
                  | GST_VIDEO_FORMAT_FLAG_GRAY)) {
            gst_value_list_append_value (&passthrough_formats, val);
          } else if (GST_VIDEO_FORMAT_INFO_FLAGS (t_info) &
              GST_VIDEO_FORMAT_FLAG_RGB) {
            have_rgb_formats = TRUE;
            break;
          }
        }
      }
      if (have_rgb_formats) {
        gst_structure_remove_fields (st, "format", NULL);
      } else {
        /* add passthrough structure, then the rgb conversion structure */
        gst_structure_set_value (st, "format", &passthrough_formats);
        gst_caps_append_structure_full (res, gst_structure_copy (st),
            gst_caps_features_copy (f));
        gst_structure_set_value (st, "format", &rgb_formats);
      }
      g_value_unset (&passthrough_formats);
    } else if (G_VALUE_HOLDS_STRING (format)) {
      const gchar *format_str = g_value_get_string (format);
      GstVideoFormat v_format = gst_video_format_from_string (format_str);
      const GstVideoFormatInfo *t_info = gst_video_format_get_info (v_format);
      if (GST_VIDEO_FORMAT_INFO_FLAGS (t_info) & (GST_VIDEO_FORMAT_FLAG_YUV |
              GST_VIDEO_FORMAT_FLAG_GRAY)) {
        /* add passthrough structure, then the rgb conversion structure */
        gst_structure_set_value (st, "format", format);
        gst_caps_append_structure_full (res, gst_structure_copy (st),
            gst_caps_features_copy (f));
        gst_structure_set_value (st, "format", &rgb_formats);
      } else {                  /* RGB */
        gst_structure_remove_fields (st, "format", NULL);
      }
    }
    gst_structure_remove_fields (st, "colorimetry", "chroma-site",
        "texture-target", NULL);

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  g_value_unset (&rgb_formats);

  return res;
}

/**
 * gst_gl_color_convert_transform_caps:
 * @context: a #GstGLContext to use for transforming @caps
 * @direction: a #GstPadDirection
 * @caps: (transfer none): the #GstCaps to transform
 * @filter: (transfer none): a set of filter #GstCaps
 *
 * Provides an implementation of #GstBaseTransformClass::transform_caps()
 *
 * Returns: (transfer full): the converted #GstCaps
 *
 * Since: 1.6
 */
GstCaps *
gst_gl_color_convert_transform_caps (GstGLContext * context,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  caps = gst_gl_color_convert_caps_transform_format_info (caps);

  if (filter) {
    GstCaps *tmp;

    tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  return caps;
}

/* fixation from videoconvert */
#define SCORE_FORMAT_CHANGE             1
#define SCORE_DEPTH_CHANGE              1
#define SCORE_ALPHA_CHANGE              1
#define SCORE_CHROMA_W_CHANGE           1
#define SCORE_CHROMA_H_CHANGE           1
#define SCORE_PALETTE_CHANGE            1

#define SCORE_COLORSPACE_LOSS           2       /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS                4       /* change bit depth */
#define SCORE_ALPHA_LOSS                8       /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS             16      /* vertical subsample */
#define SCORE_CHROMA_H_LOSS             32      /* horizontal subsample */
#define SCORE_PALETTE_LOSS              64      /* convert to palette format */
#define SCORE_COLOR_LOSS                128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

static GstGLTextureTarget
_texture_target_demask (guint target_mask)
{
  if (target_mask & (1 << GST_GL_TEXTURE_TARGET_2D)) {
    return GST_GL_TEXTURE_TARGET_2D;
  }
  if (target_mask & (1 << GST_GL_TEXTURE_TARGET_RECTANGLE)) {
    return GST_GL_TEXTURE_TARGET_RECTANGLE;
  }
  if (target_mask & (1 << GST_GL_TEXTURE_TARGET_EXTERNAL_OES)) {
    return GST_GL_TEXTURE_TARGET_EXTERNAL_OES;
  }

  return 0;
}

/* calculate how much loss a conversion would be */
static void
score_format_target (const GstVideoFormatInfo * in_info, guint targets_mask,
    GstVideoFormat v_format, guint other_targets_mask, gint * min_loss,
    const GstVideoFormatInfo ** out_info, GstGLTextureTarget * result)
{
  const GstVideoFormatInfo *t_info;
  GstVideoFormatFlags in_flags, t_flags;
  gint loss;

  t_info = gst_video_format_get_info (v_format);
  if (!t_info)
    return;

  /* accept input format immediately without loss */
  if (in_info == t_info && (targets_mask & other_targets_mask) != 0) {
    *min_loss = 0;
    *out_info = t_info;
    *result = _texture_target_demask (targets_mask & other_targets_mask);
    return;
  }

  /* can only passthrough external-oes textures */
  other_targets_mask &= ~(1 << GST_GL_TEXTURE_TARGET_EXTERNAL_OES);
  if (other_targets_mask == 0)
    return;
  /* try to keep the same target */
  if (targets_mask & other_targets_mask)
    other_targets_mask = targets_mask & other_targets_mask;

  loss = SCORE_FORMAT_CHANGE;

  in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info);
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  t_flags = GST_VIDEO_FORMAT_INFO_FLAGS (t_info);
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  /* GRAY/YUV -> GRAY/YUV is not supported */
  if ((in_flags & (GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_GRAY)) != 0
      && (t_flags & (GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_GRAY)) !=
      0)
    return;

  if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK)) {
    loss += SCORE_PALETTE_CHANGE;
    if (t_flags & PALETTE_MASK)
      loss += SCORE_PALETTE_LOSS;
  }

  if ((t_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK)) {
    loss += SCORE_COLORSPACE_LOSS;
    if (t_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
      loss += SCORE_COLOR_LOSS;
  }

  if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK)) {
    loss += SCORE_ALPHA_CHANGE;
    if (in_flags & ALPHA_MASK)
      loss += SCORE_ALPHA_LOSS;
  }

  if ((in_info->h_sub[1]) != (t_info->h_sub[1])) {
    loss += SCORE_CHROMA_H_CHANGE;
    if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
      loss += SCORE_CHROMA_H_LOSS;
  }
  if ((in_info->w_sub[1]) != (t_info->w_sub[1])) {
    loss += SCORE_CHROMA_W_CHANGE;
    if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
      loss += SCORE_CHROMA_W_LOSS;
  }

  if ((in_info->bits) != (t_info->bits)) {
    loss += SCORE_DEPTH_CHANGE;
    if ((in_info->bits) > (t_info->bits))
      loss += SCORE_DEPTH_LOSS;
  }

  if (loss < *min_loss) {
    GstGLTextureTarget target = _texture_target_demask (other_targets_mask);

    if (target != 0) {
      *out_info = t_info;
      *min_loss = loss;
      *result = target;
    }
  }
}

static void
gst_gl_color_convert_fixate_format_target (GstCaps * caps, GstCaps * result)
{
  GstStructure *ins, *outs;
  const gchar *in_format;
  const GstVideoFormatInfo *in_info, *out_info = NULL;
  const GValue *targets;
  guint targets_mask = 0;
  GstGLTextureTarget target;
  gint min_loss = G_MAXINT;
  guint i, capslen;

  ins = gst_caps_get_structure (caps, 0);
  in_format = gst_structure_get_string (ins, "format");
  if (!in_format)
    return;
  targets = gst_structure_get_value (ins, "texture-target");
  targets_mask = gst_gl_value_get_texture_target_mask (targets);
  if (!targets_mask)
    return;

  in_info =
      gst_video_format_get_info (gst_video_format_from_string (in_format));
  if (!in_info)
    return;

  outs = gst_caps_get_structure (result, 0);

  capslen = gst_caps_get_size (result);
  for (i = 0; i < capslen; i++) {
    GstStructure *tests;
    const GValue *format;
    const GValue *other_targets;
    guint other_targets_mask = 0;

    tests = gst_caps_get_structure (result, i);

    format = gst_structure_get_value (tests, "format");
    other_targets = gst_structure_get_value (tests, "texture-target");
    /* should not happen */
    if (format == NULL || other_targets == NULL)
      continue;

    other_targets_mask = gst_gl_value_get_texture_target_mask (other_targets);

    if (GST_VALUE_HOLDS_LIST (format)) {
      gint j, len;

      len = gst_value_list_get_size (format);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          const gchar *format_str = g_value_get_string (val);
          GstVideoFormat v_format = gst_video_format_from_string (format_str);
          score_format_target (in_info, targets_mask, v_format,
              other_targets_mask, &min_loss, &out_info, &target);
          if (min_loss == 0)
            break;
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format)) {
      const gchar *format_str = g_value_get_string (format);
      GstVideoFormat v_format = gst_video_format_from_string (format_str);
      score_format_target (in_info, targets_mask, v_format, other_targets_mask,
          &min_loss, &out_info, &target);
    }
  }
  if (out_info)
    gst_structure_set (outs, "format", G_TYPE_STRING,
        GST_VIDEO_FORMAT_INFO_NAME (out_info), NULL);
  if (target)
    gst_structure_set (outs, "texture-target", G_TYPE_STRING,
        gst_gl_texture_target_to_string (target), NULL);
}

/**
 * gst_gl_color_convert_fixate_caps:
 * @context: a #GstGLContext to use for transforming @caps
 * @direction: a #GstPadDirection
 * @caps: (transfer none): the #GstCaps of @direction
 * @other: (transfer full): the #GstCaps to fixate
 *
 * Provides an implementation of #GstBaseTransformClass::fixate_caps()
 *
 * Returns: (transfer full): the fixated #GstCaps
 *
 * Since: 1.8
 */
GstCaps *
gst_gl_color_convert_fixate_caps (GstGLContext * context,
    GstPadDirection direction, GstCaps * caps, GstCaps * other)
{
  GstCaps *result;

  result = gst_caps_intersect (other, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = other;
  } else {
    gst_caps_unref (other);
  }

  result = gst_caps_make_writable (result);
  gst_gl_color_convert_fixate_format_target (caps, result);

  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    }
  }

  return result;
}

/**
 * gst_gl_color_convert_perform:
 * @convert: a #GstGLColorConvert
 * @inbuf: (transfer none): the #GstGLMemory filled #GstBuffer to convert
 *
 * Converts the data contained by @inbuf using the formats specified by the
 * #GstCaps passed to gst_gl_color_convert_set_caps()
 *
 * Returns: (transfer full): a converted #GstBuffer or %NULL
 *
 * Since: 1.4
 */
GstBuffer *
gst_gl_color_convert_perform (GstGLColorConvert * convert, GstBuffer * inbuf)
{
  GstBuffer *ret;

  g_return_val_if_fail (convert != NULL, FALSE);

  GST_OBJECT_LOCK (convert);
  ret = _gst_gl_color_convert_perform_unlocked (convert, inbuf);
  GST_OBJECT_UNLOCK (convert);

  return ret;
}

static GstBuffer *
_gst_gl_color_convert_perform_unlocked (GstGLColorConvert * convert,
    GstBuffer * inbuf)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (inbuf, FALSE);

  if (G_UNLIKELY (convert->passthrough))
    return gst_buffer_ref (inbuf);

  convert->inbuf = inbuf;

  gst_gl_context_thread_add (convert->context,
      (GstGLContextThreadFunc) _do_convert, convert);

  if (!convert->priv->result) {
    if (convert->outbuf)
      gst_buffer_unref (convert->outbuf);
    convert->outbuf = NULL;
    return NULL;
  }

  return convert->outbuf;
}

static inline gboolean
_is_RGBx (GstVideoFormat v_format)
{
  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
      return TRUE;
    default:
      return FALSE;
  }
}

static inline gchar
_index_to_shader_swizzle (int idx)
{
  switch (idx) {
    case 0:
      return 'r';
    case 1:
      return 'g';
    case 2:
      return 'b';
    case 3:
      return 'a';
    default:
      return '#';
  }
}

/* attempts to transform expected to want using swizzling */
static gchar *
_RGB_pixel_order (const gchar * expected, const gchar * wanted)
{
  GString *ret = g_string_sized_new (4);
  gchar *expect, *want;
  gchar *orig_want;
  int len;
  gboolean discard_output = TRUE;

  if (g_ascii_strcasecmp (expected, wanted) == 0) {
    g_string_free (ret, TRUE);
    return g_ascii_strdown (expected, -1);
  }

  expect = g_ascii_strdown (expected, -1);
  orig_want = want = g_ascii_strdown (wanted, -1);

  if (strcmp (expect, "rgb16") == 0 || strcmp (expect, "bgr16") == 0) {
    gchar *temp = expect;
    expect = g_strndup (temp, 3);
    g_free (temp);
  }

  if (strcmp (want, "rgb16") == 0 || strcmp (want, "bgr16") == 0) {
    gchar *temp = want;
    orig_want = want = g_strndup (temp, 3);
    g_free (temp);
  }

  /* pad want with 'a's */
  if ((len = strlen (want)) < 4) {
    gchar *new_want = g_strndup (want, 4);
    while (len < 4) {
      new_want[len] = 'a';
      len++;
    }
    g_free (want);
    orig_want = want = new_want;
  }

  /* pad expect with 'a's */
  if ((len = strlen (expect)) < 4) {
    gchar *new_expect = g_strndup (expect, 4);
    while (len < 4) {
      new_expect[len] = 'a';
      len++;
    }
    g_free (expect);
    expect = new_expect;
  }

  /* build the swizzle format */
  while (want && want[0] != '\0') {
    gchar *val;
    gint idx;
    gchar needle = want[0];

    if (needle == 'x')
      needle = 'a';

    if (!(val = strchr (expect, needle))
        && needle == 'a' && !(val = strchr (expect, 'x')))
      goto out;

    idx = (gint) (val - expect);

    ret = g_string_append_c (ret, _index_to_shader_swizzle (idx));
    want = &want[1];
  }

  discard_output = FALSE;
out:
  g_free (orig_want);
  g_free (expect);

  return g_string_free (ret, discard_output);
}

static guint
_get_n_textures (GstVideoFormat v_format)
{
  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return 1;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      return 2;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_YV12:
      return 3;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static void
_RGB_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  const gchar *in_format_str = gst_video_format_to_string (in_format);
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, out_format_str);
  gchar *alpha = NULL;

  if (_is_RGBx (in_format)) {
    int i;
    char input_alpha_channel = 'a';
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (in_format_str[i] == 'X' || in_format_str[i] == 'x') {
        input_alpha_channel = _index_to_shader_swizzle (i);
        break;
      }
    }
    alpha = g_strdup_printf ("t.%c = 1.0;", input_alpha_channel);
  }
  info->templ = &templ_REORDER;
  info->frag_body = g_strdup_printf (info->templ->body, alpha ? alpha : "",
      pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
  info->shader_tex_names[0] = "tex";

  g_free (alpha);
  g_free (pixel_order);
}

static void
_YUV_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order ("rgba", out_format_str);
  gboolean apple_ycbcr = gst_gl_context_check_feature (convert->context,
      "GL_APPLE_ycbcr_422");
  gboolean in_tex_rectangular = FALSE;

#if GST_GL_HAVE_OPENGL
  GstMemory *memory = gst_buffer_peek_memory (convert->inbuf, 0);
  if (gst_is_gl_memory (memory) && (USING_OPENGL (convert->context)
          || USING_OPENGL3 (convert->context))) {
    in_tex_rectangular =
        convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_RECTANGLE;
  }
#endif

  info->out_n_textures = 1;

  if (in_tex_rectangular && apple_ycbcr
      && gst_buffer_n_memory (convert->inbuf) == 1) {
    /* FIXME: We should probably also check if tex_target actually is using
     * the Apple YCbCr422 extension. It could also be a normal UYVY texture
     * with RB or Lum/Alpha
     */
    /* The mangling will change this to the correct texture2DRect, sampler2DRect
     * for us */
    info->templ = &templ_REORDER;
    info->frag_body =
        g_strdup_printf (info->templ->body, pixel_order[0], pixel_order[1],
        pixel_order[2], pixel_order[3]);
    info->shader_tex_names[0] = "tex";
  } else {
    switch (GST_VIDEO_INFO_FORMAT (&convert->in_info)) {
      case GST_VIDEO_FORMAT_AYUV:
        info->templ = &templ_AYUV_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, pixel_order[0],
            pixel_order[1], pixel_order[2], pixel_order[3]);
        info->shader_tex_names[0] = "tex";
        break;
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_Y444:
      case GST_VIDEO_FORMAT_Y42B:
      case GST_VIDEO_FORMAT_Y41B:
        info->templ = &templ_PLANAR_YUV_to_RGB;
        info->frag_body =
            g_strdup_printf (info->templ->body, pixel_order[0],
            pixel_order[1], pixel_order[2], pixel_order[3]);
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "Utex";
        info->shader_tex_names[2] = "Vtex";
        break;
      case GST_VIDEO_FORMAT_YV12:
        info->templ = &templ_PLANAR_YUV_to_RGB;
        info->frag_body =
            g_strdup_printf (info->templ->body, pixel_order[0],
            pixel_order[1], pixel_order[2], pixel_order[3]);
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "Vtex";
        info->shader_tex_names[2] = "Utex";
        break;
      case GST_VIDEO_FORMAT_YUY2:
      {
        char uv_val = convert->priv->in_tex_formats[0] == GST_GL_RG ? 'g' : 'a';
        info->templ = &templ_YUY2_UYVY_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, 'r', uv_val,
            uv_val, 'g', 'a', pixel_order[0], pixel_order[1], pixel_order[2],
            pixel_order[3]);
        info->shader_tex_names[0] = "Ytex";
        break;
      }
      case GST_VIDEO_FORMAT_UYVY:
      {
        char y_val = convert->priv->in_tex_formats[0] == GST_GL_RG ? 'g' : 'a';
        info->templ = &templ_YUY2_UYVY_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, y_val, 'g',
            'g', 'r', 'b', pixel_order[0], pixel_order[1], pixel_order[2],
            pixel_order[3]);
        info->shader_tex_names[0] = "Ytex";
        break;
      }
      case GST_VIDEO_FORMAT_NV12:
      {
        char val2 = convert->priv->in_tex_formats[1] == GST_GL_RG ? 'g' : 'a';
        info->templ = &templ_NV12_NV21_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, 'r', val2,
            pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "UVtex";
        break;
      }
      case GST_VIDEO_FORMAT_NV21:
      {
        char val2 = convert->priv->in_tex_formats[1] == GST_GL_RG ? 'g' : 'a';
        info->templ = &templ_NV12_NV21_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, val2, 'r',
            pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "UVtex";
        break;
      }
      default:
        break;
    }
  }

  if (gst_video_colorimetry_matches (&convert->in_info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT709)) {
    info->cms_offset = (gfloat *) from_yuv_bt709_offset;
    info->cms_coeff1 = (gfloat *) from_yuv_bt709_rcoeff;
    info->cms_coeff2 = (gfloat *) from_yuv_bt709_gcoeff;
    info->cms_coeff3 = (gfloat *) from_yuv_bt709_bcoeff;
  } else {
    /* defaults/bt601 */
    info->cms_offset = (gfloat *) from_yuv_bt601_offset;
    info->cms_coeff1 = (gfloat *) from_yuv_bt601_rcoeff;
    info->cms_coeff2 = (gfloat *) from_yuv_bt601_gcoeff;
    info->cms_coeff3 = (gfloat *) from_yuv_bt601_bcoeff;
  }

  g_free (pixel_order);
}

static void
_RGB_to_YUV (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  const gchar *in_format_str = gst_video_format_to_string (in_format);
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, "rgba");
  const gchar *alpha;

  info->frag_prog = NULL;

  info->shader_tex_names[0] = "tex";

  switch (out_format) {
    case GST_VIDEO_FORMAT_AYUV:
      alpha = _is_RGBx (in_format) ? "1.0" : "texel.a";
      info->templ = &templ_RGB_to_AYUV;
      info->frag_body = g_strdup_printf (info->templ->body, pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3], alpha);
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      info->templ = &templ_RGB_to_PLANAR_YUV;
      info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->out_n_textures = 3;
      if (out_format == GST_VIDEO_FORMAT_Y444) {
        info->chroma_sampling[0] = info->chroma_sampling[1] = 1.0f;
      } else if (out_format == GST_VIDEO_FORMAT_Y42B) {
        info->chroma_sampling[0] = 2.0f;
        info->chroma_sampling[1] = 1.0f;
      } else if (out_format == GST_VIDEO_FORMAT_Y41B) {
        info->chroma_sampling[0] = 4.0f;
        info->chroma_sampling[1] = 1.0f;
      } else {
        info->chroma_sampling[0] = info->chroma_sampling[1] = 2.0f;
      }
      break;
    case GST_VIDEO_FORMAT_YUY2:
      info->templ = &templ_RGB_to_YUY2_UYVY;
      info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'x', 'y', 'x', 'z');
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      info->templ = &templ_RGB_to_YUY2_UYVY,
          info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'y', 'x', 'z', 'x');
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_NV12:
      info->templ = &templ_RGB_to_NV12_NV21,
          info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'y', 'z');
      info->out_n_textures = 2;
      break;
    case GST_VIDEO_FORMAT_NV21:
      info->templ = &templ_RGB_to_NV12_NV21,
          info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'z', 'y');
      info->out_n_textures = 2;
      break;
    default:
      break;
  }

  if (gst_video_colorimetry_matches (&convert->in_info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT709)) {
    info->cms_offset = (gfloat *) from_rgb_bt709_offset;
    info->cms_coeff1 = (gfloat *) from_rgb_bt709_ycoeff;
    info->cms_coeff2 = (gfloat *) from_rgb_bt709_ucoeff;
    info->cms_coeff3 = (gfloat *) from_rgb_bt709_vcoeff;
  } else {
    /* defaults/bt601 */
    info->cms_offset = (gfloat *) from_rgb_bt601_offset;
    info->cms_coeff1 = (gfloat *) from_rgb_bt601_ycoeff;
    info->cms_coeff2 = (gfloat *) from_rgb_bt601_ucoeff;
    info->cms_coeff3 = (gfloat *) from_rgb_bt601_vcoeff;
  }

  g_free (pixel_order);
}

static void
_RGB_to_GRAY (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  const gchar *in_format_str = gst_video_format_to_string (in_format);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, "rgba");
  gchar *alpha = NULL;

  info->out_n_textures = 1;
  info->shader_tex_names[0] = "tex";

  if (_is_RGBx (in_format))
    alpha = g_strdup_printf ("t.%c = 1.0;", pixel_order[3]);

  switch (GST_VIDEO_INFO_FORMAT (&convert->out_info)) {
    case GST_VIDEO_FORMAT_GRAY8:
      info->templ = &templ_REORDER;
      info->frag_body = g_strdup_printf (info->templ->body, alpha ? alpha : "",
          pixel_order[0], pixel_order[0], pixel_order[0], pixel_order[3]);
      break;
    default:
      break;
  }

  g_free (alpha);
  g_free (pixel_order);
}

static void
_GRAY_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order ("rgba", out_format_str);

  info->shader_tex_names[0] = "tex";

  switch (GST_VIDEO_INFO_FORMAT (&convert->in_info)) {
    case GST_VIDEO_FORMAT_GRAY8:
      info->templ = &templ_REORDER;
      info->frag_body = g_strdup_printf (info->templ->body, "", pixel_order[0],
          pixel_order[0], pixel_order[0], pixel_order[3]);
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
    {
      char val2 = convert->priv->in_tex_formats[0] == GST_GL_RG ? 'g' : 'a';
      info->templ = &templ_COMPOSE;
      info->frag_body = g_strdup_printf (info->templ->body, val2, 'r',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    }
    case GST_VIDEO_FORMAT_GRAY16_BE:
    {
      char val2 = convert->priv->in_tex_formats[0] == GST_GL_RG ? 'g' : 'a';
      info->templ = &templ_COMPOSE;
      info->frag_body = g_strdup_printf (info->templ->body, 'r', val2,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    }
    default:
      break;
  }

  g_free (pixel_order);
}

static void
_bind_buffer (GstGLColorConvert * convert)
{
  const GstGLFuncs *gl = convert->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, convert->priv->vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, convert->priv->vertex_buffer);

  /* Load the vertex position */
  gl->VertexAttribPointer (convert->priv->attr_position, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (convert->priv->attr_texture, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (convert->priv->attr_position);
  gl->EnableVertexAttribArray (convert->priv->attr_texture);
}

static void
_unbind_buffer (GstGLColorConvert * convert)
{
  const GstGLFuncs *gl = convert->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (convert->priv->attr_position);
  gl->DisableVertexAttribArray (convert->priv->attr_texture);
}

static GstGLShader *
_create_shader (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GString *str = g_string_new (NULL);
  GstGLShader *ret = NULL;
  GstGLSLStage *stage;
  GstGLSLVersion version;
  GstGLSLProfile profile;
  gchar *version_str, *tmp, *tmp1;
  const gchar *strings[2];
  GError *error = NULL;
  int i;

  ret = gst_gl_shader_new (convert->context);

  tmp =
      _gst_glsl_mangle_shader (text_vertex_shader, GL_VERTEX_SHADER,
      info->templ->target, convert->priv->from_texture_target, convert->context,
      &version, &profile);

  tmp1 = gst_glsl_version_profile_to_string (version, profile);
  version_str = g_strdup_printf ("#version %s\n", tmp1);
  g_free (tmp1);

  strings[0] = version_str;
  strings[1] = tmp;
  if (!(stage = gst_glsl_stage_new_with_strings (convert->context,
              GL_VERTEX_SHADER, version, profile, 2, strings))) {
    GST_ERROR_OBJECT (convert, "Failed to create vertex stage");
    g_free (version_str);
    g_free (tmp);
    gst_object_unref (ret);
    return NULL;
  }
  g_free (tmp);

  if (!gst_gl_shader_compile_attach_stage (ret, stage, &error)) {
    GST_ERROR_OBJECT (convert, "Failed to compile vertex shader %s",
        error->message);
    g_clear_error (&error);
    g_free (version_str);
    gst_object_unref (stage);
    gst_object_unref (ret);
    return NULL;
  }

  if (info->templ->extensions)
    g_string_append (str, info->templ->extensions);

  if (convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_EXTERNAL_OES
      && info->templ->target != GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
    g_string_append (str, glsl_OES_extension_string);

  if (info->templ->uniforms)
    g_string_append (str, info->templ->uniforms);

  g_string_append_c (str, '\n');

  /* GL 3.3+ and GL ES 3.x */
  if ((profile == GST_GLSL_PROFILE_CORE && version >= GST_GLSL_VERSION_330)
      || (profile == GST_GLSL_PROFILE_ES && version >= GST_GLSL_VERSION_300)) {
    if (info->out_n_textures > 1) {
      gint i;

      for (i = 0; i < info->out_n_textures; i++) {
        g_string_append_printf (str,
            "layout(location = %d) out vec4 fragColor_%d;\n", i, i);
      }
    } else {
      g_string_append (str, "layout (location = 0) out vec4 fragColor;\n");
    }
  } else if (profile == GST_GLSL_PROFILE_CORE
      && version >= GST_GLSL_VERSION_150) {
    /* no layout specifiers, use glBindFragDataLocation instead */
    if (info->out_n_textures > 1) {
      gint i;

      for (i = 0; i < info->out_n_textures; i++) {
        gchar *var_name = g_strdup_printf ("fragColor_%d", i);
        g_string_append_printf (str, "out vec4 %s;\n", var_name);
        gst_gl_shader_bind_frag_data_location (ret, i, var_name);
        g_free (var_name);
      }
    } else {
      g_string_append (str, "out vec4 fragColor;\n");
      gst_gl_shader_bind_frag_data_location (ret, 0, "fragColor");
    }
  }

  for (i = 0; i < MAX_FUNCTIONS; i++) {
    if (info->templ->functions[i] == NULL)
      break;

    g_string_append_c (str, '\n');
    g_string_append (str, info->templ->functions[i]);
    g_string_append_c (str, '\n');
  }

  {
    const gchar *varying = NULL;

    if ((profile == GST_GLSL_PROFILE_ES && version >= GST_GLSL_VERSION_300)
        || (profile == GST_GLSL_PROFILE_CORE
            && version >= GST_GLSL_VERSION_150)) {
      varying = "in";
    } else {
      varying = "varying";
    }
    g_string_append_printf (str, "\n%s vec2 v_texcoord;\nvoid main (void) {\n",
        varying);
  }
  if (info->frag_body) {
    g_string_append (str, "vec2 texcoord;\n");
    if (convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_RECTANGLE
        && info->templ->target != GST_GL_TEXTURE_TARGET_RECTANGLE) {
      g_string_append (str, "texcoord = v_texcoord * vec2 (width, height);\n");
    } else {
      g_string_append (str, "texcoord = v_texcoord;\n");
    }

    g_string_append (str, info->frag_body);
  }
  g_string_append (str, "\n}");
  tmp = g_string_free (str, FALSE);
  info->frag_prog = _gst_glsl_mangle_shader (tmp, GL_FRAGMENT_SHADER,
      info->templ->target, convert->priv->from_texture_target, convert->context,
      &version, &profile);
  g_free (tmp);

  strings[1] = info->frag_prog;
  if (!(stage = gst_glsl_stage_new_with_strings (convert->context,
              GL_FRAGMENT_SHADER, version, profile, 2, strings))) {
    GST_ERROR_OBJECT (convert, "Failed to create fragment stage");
    g_free (info->frag_prog);
    info->frag_prog = NULL;
    g_free (version_str);
    gst_object_unref (ret);
    return NULL;
  }
  g_free (version_str);
  if (!gst_gl_shader_compile_attach_stage (ret, stage, &error)) {
    GST_ERROR_OBJECT (convert, "Failed to compile fragment shader %s",
        error->message);
    g_clear_error (&error);
    g_free (info->frag_prog);
    info->frag_prog = NULL;
    gst_object_unref (stage);
    gst_object_unref (ret);
    return NULL;
  }

  if (!gst_gl_shader_link (ret, &error)) {
    GST_ERROR_OBJECT (convert, "Failed to link shader %s", error->message);
    g_clear_error (&error);
    g_free (info->frag_prog);
    info->frag_prog = NULL;
    gst_object_unref (ret);
    return NULL;
  }

  return ret;
}

/* Called in the gl thread */
static gboolean
_init_convert (GstGLColorConvert * convert)
{
  GstGLFuncs *gl;
  struct ConvertInfo *info = &convert->priv->convert_info;
  gint i;

  gl = convert->context->gl_vtable;

  if (convert->initted)
    return TRUE;

  GST_INFO ("Initializing color conversion from %s to %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)));

  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    GST_ERROR_OBJECT (convert, "Cannot perform color conversion without "
        "OpenGL shaders");
    goto error;
  }

  if (GST_VIDEO_INFO_IS_RGB (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (&convert->out_info)) {
      _RGB_to_RGB (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_YUV (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (&convert->out_info)) {
      _YUV_to_RGB (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_RGB (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_YUV (&convert->out_info)) {
      _RGB_to_YUV (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_RGB (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_GRAY (&convert->out_info)) {
      _RGB_to_GRAY (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_GRAY (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (&convert->out_info)) {
      _GRAY_to_RGB (convert);
    }
  }

  if (!info->frag_body || info->in_n_textures == 0 || info->out_n_textures == 0)
    goto unhandled_format;

  /* multiple draw targets not supported on GLES2... */
  if (info->out_n_textures > 1 && !gl->DrawBuffers) {
    GST_ERROR ("Conversion requires output to multiple draw buffers");
    goto incompatible_api;
  }

  /* Requires reading from a RG/LA framebuffer... */
  if (USING_GLES2 (convert->context) && !USING_GLES3 (convert->context) &&
      (GST_VIDEO_INFO_FORMAT (&convert->out_info) == GST_VIDEO_FORMAT_YUY2 ||
          GST_VIDEO_INFO_FORMAT (&convert->out_info) ==
          GST_VIDEO_FORMAT_UYVY)) {
    GST_ERROR ("Conversion requires reading with an unsupported format");
    goto incompatible_api;
  }

  if (!(convert->shader = _create_shader (convert)))
    goto error;

  convert->priv->attr_position =
      gst_gl_shader_get_attribute_location (convert->shader, "a_position");
  convert->priv->attr_texture =
      gst_gl_shader_get_attribute_location (convert->shader, "a_texcoord");

  gst_gl_shader_use (convert->shader);

  if (info->cms_offset && info->cms_coeff1
      && info->cms_coeff2 && info->cms_coeff3) {
    gst_gl_shader_set_uniform_3fv (convert->shader, "offset", 1,
        info->cms_offset);
    gst_gl_shader_set_uniform_3fv (convert->shader, "coeff1", 1,
        info->cms_coeff1);
    gst_gl_shader_set_uniform_3fv (convert->shader, "coeff2", 1,
        info->cms_coeff2);
    gst_gl_shader_set_uniform_3fv (convert->shader, "coeff3", 1,
        info->cms_coeff3);
  }

  for (i = info->in_n_textures; i >= 0; i--) {
    if (info->shader_tex_names[i])
      gst_gl_shader_set_uniform_1i (convert->shader, info->shader_tex_names[i],
          i);
  }

  gst_gl_shader_set_uniform_1f (convert->shader, "width",
      GST_VIDEO_INFO_WIDTH (&convert->in_info));
  gst_gl_shader_set_uniform_1f (convert->shader, "height",
      GST_VIDEO_INFO_HEIGHT (&convert->in_info));

  if (convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_RECTANGLE) {
    gst_gl_shader_set_uniform_1f (convert->shader, "poffset_x", 1.);
    gst_gl_shader_set_uniform_1f (convert->shader, "poffset_y", 1.);
  } else {
    gst_gl_shader_set_uniform_1f (convert->shader, "poffset_x",
        1. / (gfloat) GST_VIDEO_INFO_WIDTH (&convert->in_info));
    gst_gl_shader_set_uniform_1f (convert->shader, "poffset_y",
        1. / (gfloat) GST_VIDEO_INFO_HEIGHT (&convert->in_info));
  }

  if (info->chroma_sampling[0] > 0.0f && info->chroma_sampling[1] > 0.0f) {
    gst_gl_shader_set_uniform_2fv (convert->shader, "chroma_sampling", 1,
        info->chroma_sampling);
  }

  gst_gl_context_clear_shader (convert->context);

  if (convert->fbo == NULL && !_init_convert_fbo (convert)) {
    goto error;
  }

  if (!convert->priv->vertex_buffer) {
    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &convert->priv->vao);
      gl->BindVertexArray (convert->priv->vao);
    }

    gl->GenBuffers (1, &convert->priv->vertex_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, convert->priv->vertex_buffer);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);

    gl->GenBuffers (1, &convert->priv->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, convert->priv->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);

    if (gl->GenVertexArrays) {
      _bind_buffer (convert);
      gl->BindVertexArray (0);
    }

    gl->BindBuffer (GL_ARRAY_BUFFER, 0);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  }

  gl->BindTexture (GL_TEXTURE_2D, 0);

  convert->initted = TRUE;

  return TRUE;

unhandled_format:
  GST_ERROR_OBJECT (convert, "Don't know how to convert from %s to %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)));

error:
  return FALSE;

incompatible_api:
  {
    GST_ERROR_OBJECT (convert, "Converting from %s to %s requires "
        "functionality that the current OpenGL setup does not support",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
            (&convert->out_info)));
    return FALSE;
  }
}

/* called by _init_convert (in the gl thread) */
static gboolean
_init_convert_fbo (GstGLColorConvert * convert)
{
  guint out_width, out_height;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);

  convert->fbo =
      gst_gl_framebuffer_new_with_default_depth (convert->context, out_width,
      out_height);

  return convert->fbo != NULL;
}

static gboolean
_do_convert_one_view (GstGLContext * context, GstGLColorConvert * convert,
    guint view_num)
{
  guint in_width, in_height, out_width, out_height;
  struct ConvertInfo *c_info = &convert->priv->convert_info;
  GstMapInfo out_info[GST_VIDEO_MAX_PLANES], in_info[GST_VIDEO_MAX_PLANES];
  gboolean res = TRUE;
  gint i, j = 0;
  const gint in_plane_offset = view_num * c_info->in_n_textures;
  const gint out_plane_offset = view_num * c_info->out_n_textures;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);
  in_width = GST_VIDEO_INFO_WIDTH (&convert->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&convert->in_info);

  for (i = 0; i < c_info->in_n_textures; i++) {
    convert->priv->in_tex[i] =
        (GstGLMemory *) gst_buffer_peek_memory (convert->inbuf,
        i + in_plane_offset);
    if (!gst_is_gl_memory ((GstMemory *) convert->priv->in_tex[i])) {
      GST_ERROR_OBJECT (convert, "input must be GstGLMemory");
      res = FALSE;
      goto out;
    }
    if (!gst_memory_map ((GstMemory *) convert->priv->in_tex[i], &in_info[i],
            GST_MAP_READ | GST_MAP_GL)) {
      GST_ERROR_OBJECT (convert, "failed to map input memory %p",
          convert->priv->in_tex[i]);
      res = FALSE;
      goto out;
    }
  }

  for (j = 0; j < c_info->out_n_textures; j++) {
    GstGLMemory *out_tex =
        (GstGLMemory *) gst_buffer_peek_memory (convert->outbuf,
        j + out_plane_offset);
    gint mem_width, mem_height;

    if (!gst_is_gl_memory ((GstMemory *) out_tex)) {
      GST_ERROR_OBJECT (convert, "output must be GstGLMemory");
      res = FALSE;
      goto out;
    }

    mem_width = gst_gl_memory_get_texture_width (out_tex);
    mem_height = gst_gl_memory_get_texture_height (out_tex);

    if (out_tex->tex_format == GST_GL_LUMINANCE
        || out_tex->tex_format == GST_GL_LUMINANCE_ALPHA
        || out_width != mem_width || out_height != mem_height) {
      /* Luminance formats are not color renderable */
      /* renderering to a framebuffer only renders the intersection of all
       * the attachments i.e. the smallest attachment size */
      if (!convert->priv->out_tex[j]) {
        GstGLVideoAllocationParams *params;
        GstGLBaseMemoryAllocator *base_mem_allocator;
        GstAllocator *allocator;
        GstVideoInfo temp_info;

        gst_video_info_set_format (&temp_info, GST_VIDEO_FORMAT_RGBA, out_width,
            out_height);

        allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR_NAME);
        base_mem_allocator = GST_GL_BASE_MEMORY_ALLOCATOR (allocator);
        params = gst_gl_video_allocation_params_new (context, NULL, &temp_info,
            0, NULL, convert->priv->to_texture_target, GST_GL_RGBA);

        convert->priv->out_tex[j] =
            (GstGLMemory *) gst_gl_base_memory_alloc (base_mem_allocator,
            (GstGLAllocationParams *) params);

        gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
        gst_object_unref (allocator);
      }
    } else {
      convert->priv->out_tex[j] = out_tex;
    }

    if (!gst_memory_map ((GstMemory *) convert->priv->out_tex[j], &out_info[j],
            GST_MAP_WRITE | GST_MAP_GL)) {
      GST_ERROR_OBJECT (convert, "failed to map output memory %p",
          convert->priv->out_tex[i]);
      res = FALSE;
      goto out;
    }
  }

  GST_LOG_OBJECT (convert, "converting to textures:%p,%p,%p,%p "
      "dimensions:%ux%u, from textures:%p,%p,%p,%p dimensions:%ux%u",
      convert->priv->out_tex[0], convert->priv->out_tex[1],
      convert->priv->out_tex[2], convert->priv->out_tex[3], out_width,
      out_height, convert->priv->in_tex[0], convert->priv->in_tex[1],
      convert->priv->in_tex[2], convert->priv->in_tex[3], in_width, in_height);

  if (!_do_convert_draw (context, convert))
    res = FALSE;

out:
  for (j--; j >= 0; j--) {
    GstGLMemory *out_tex =
        (GstGLMemory *) gst_buffer_peek_memory (convert->outbuf,
        j + out_plane_offset);
    gint mem_width, mem_height;

    gst_memory_unmap ((GstMemory *) convert->priv->out_tex[j], &out_info[j]);

    mem_width = gst_gl_memory_get_texture_width (out_tex);
    mem_height = gst_gl_memory_get_texture_height (out_tex);

    if (out_tex->tex_format == GST_GL_LUMINANCE
        || out_tex->tex_format == GST_GL_LUMINANCE_ALPHA
        || out_width != mem_width || out_height != mem_height) {
      GstMapInfo to_info, from_info;

      if (!gst_memory_map ((GstMemory *) convert->priv->out_tex[j], &from_info,
              GST_MAP_READ | GST_MAP_GL)) {
        GST_ERROR_OBJECT (convert, "Failed to map intermediate memory");
        res = FALSE;
        continue;
      }
      if (!gst_memory_map ((GstMemory *) out_tex, &to_info,
              GST_MAP_WRITE | GST_MAP_GL)) {
        GST_ERROR_OBJECT (convert, "Failed to map intermediate memory");
        res = FALSE;
        continue;
      }
      gst_gl_memory_copy_into (convert->priv->out_tex[j],
          out_tex->tex_id, convert->priv->to_texture_target,
          out_tex->tex_format, mem_width, mem_height);
      gst_memory_unmap ((GstMemory *) convert->priv->out_tex[j], &from_info);
      gst_memory_unmap ((GstMemory *) out_tex, &to_info);
    } else {
      convert->priv->out_tex[j] = NULL;
    }
  }

  /* YV12 the same as I420 except planes 1+2 swapped */
  if (GST_VIDEO_INFO_FORMAT (&convert->out_info) == GST_VIDEO_FORMAT_YV12) {
    GstMemory *mem1 =
        gst_buffer_get_memory (convert->outbuf, 1 + out_plane_offset);
    GstMemory *mem2 =
        gst_buffer_get_memory (convert->outbuf, 2 + out_plane_offset);

    gst_buffer_replace_memory (convert->outbuf, 1 + out_plane_offset, mem2);
    gst_buffer_replace_memory (convert->outbuf, 2 + out_plane_offset, mem1);
  }

  for (i--; i >= 0; i--) {
    gst_memory_unmap ((GstMemory *) convert->priv->in_tex[i], &in_info[i]);
  }

  return res;
}

/* Called by the idle function in the gl thread */
void
_do_convert (GstGLContext * context, GstGLColorConvert * convert)
{
  GstVideoInfo *in_info = &convert->in_info;
  struct ConvertInfo *c_info = &convert->priv->convert_info;
  gboolean res = TRUE;
  gint views, v;
  GstVideoOverlayCompositionMeta *composition_meta;
  GstGLSyncMeta *sync_meta;
  GstFlowReturn ret;

  convert->outbuf = NULL;

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (in_info) ==
      GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
    views = GST_VIDEO_INFO_VIEWS (in_info);
  else
    views = 1;

  c_info->in_n_textures =
      _get_n_textures (GST_VIDEO_INFO_FORMAT (&convert->in_info));
  c_info->out_n_textures =
      _get_n_textures (GST_VIDEO_INFO_FORMAT (&convert->out_info));

  {
    gboolean tex_format_change = FALSE;
    guint i, v;

    for (v = 0; v < views; v++) {
      for (i = 0; i < c_info->in_n_textures; i++) {
        guint j = v * c_info->in_n_textures + i;
        GstGLMemory *in_tex =
            (GstGLMemory *) gst_buffer_peek_memory (convert->inbuf, j);
        if (!gst_is_gl_memory ((GstMemory *) in_tex)) {
          GST_ERROR_OBJECT (convert, "input must be GstGLMemory");
          convert->priv->result = FALSE;
          return;
        }

        if (j >= GST_VIDEO_MAX_PLANES)
          /* our arrays aren't that big */
          g_assert_not_reached ();

        if (v > 0 && in_tex->tex_format != convert->priv->in_tex_formats[i]) {
          GST_ERROR_OBJECT (convert, "Cannot convert textures with "
              "different types");
          convert->priv->result = FALSE;
          return;
        }

        if (convert->priv->in_tex_formats[j] != in_tex->tex_format)
          tex_format_change = TRUE;

        convert->priv->in_tex_formats[j] = in_tex->tex_format;
      }
    }

    if (tex_format_change)
      gst_gl_color_convert_reset_shader (convert);
  }

  if (!_init_convert (convert)) {
    convert->priv->result = FALSE;
    return;
  }

  sync_meta = gst_buffer_get_gl_sync_meta (convert->inbuf);
  if (sync_meta)
    gst_gl_sync_meta_wait (sync_meta, convert->context);

  if (!convert->priv->pool) {
    gboolean ret;
    /* No pool! */
    GstQuery *query = gst_query_new_allocation (convert->priv->out_caps, TRUE);
    ret = gst_gl_color_convert_decide_allocation (convert, query);
    gst_query_unref (query);

    if (!ret) {
      GST_ERROR_OBJECT (convert, "Failed to choose allocation parameters");
      convert->priv->result = FALSE;
      return;
    }

    if (!convert->priv->pool) {
      GST_ERROR_OBJECT (convert, "Failed to create a buffer pool");
      convert->priv->result = FALSE;
      return;
    }
  }

  if (!convert->priv->pool_started) {
    if (!gst_buffer_pool_set_active (convert->priv->pool, TRUE)) {
      GST_ERROR_OBJECT (convert, "Failed to start buffer pool");
      convert->priv->result = FALSE;
      return;
    }
    convert->priv->pool_started = TRUE;
  }

  ret =
      gst_buffer_pool_acquire_buffer (convert->priv->pool, &convert->outbuf,
      NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (convert, "Failed to acquire buffer from pool: %s",
        gst_flow_get_name (ret));
    convert->priv->result = FALSE;
    return;
  }

  gst_gl_insert_debug_marker (context, "%s converting from %s to %s",
      GST_OBJECT_NAME (convert),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)));
  /* Handle all views on input and output one at a time */
  for (v = 0; res && v < views; v++)
    res = _do_convert_one_view (context, convert, v);

  if (!res) {
    gst_buffer_unref (convert->outbuf);
    convert->outbuf = NULL;
  }

  if (convert->outbuf) {
    GstGLSyncMeta *sync_meta =
        gst_buffer_add_gl_sync_meta (convert->context, convert->outbuf);

    if (sync_meta)
      gst_gl_sync_meta_set_sync_point (sync_meta, convert->context);
  }

  composition_meta =
      gst_buffer_get_video_overlay_composition_meta (convert->inbuf);
  if (composition_meta) {
    GST_DEBUG ("found video overlay composition meta, appliying on output.");
    gst_buffer_add_video_overlay_composition_meta
        (convert->outbuf, composition_meta->overlay);
  }

  convert->priv->result = res;
  return;
}

static gboolean
_do_convert_draw (GstGLContext * context, GstGLColorConvert * convert)
{
  GstGLFuncs *gl;
  struct ConvertInfo *c_info = &convert->priv->convert_info;
  guint out_width, out_height;
  gint i;
  gboolean ret = TRUE;

  GLint viewport_dim[4] = { 0 };

  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2
  };

  gl = context->gl_vtable;

  gst_gl_framebuffer_bind (convert->fbo);

  /* attach the texture to the FBO to renderer to */
  for (i = 0; i < c_info->out_n_textures; i++) {
    GstGLBaseMemory *tex = (GstGLBaseMemory *) convert->priv->out_tex[i];

    gst_gl_framebuffer_attach (convert->fbo, GL_COLOR_ATTACHMENT0 + i, tex);
  }

  if (gl->DrawBuffers)
    gl->DrawBuffers (c_info->out_n_textures, multipleRT);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gst_gl_framebuffer_get_effective_dimensions (convert->fbo, &out_width,
      &out_height);
  gl->Viewport (0, 0, out_width, out_height);

  gst_gl_shader_use (convert->shader);

  if (gl->BindVertexArray)
    gl->BindVertexArray (convert->priv->vao);
  _bind_buffer (convert);

  for (i = c_info->in_n_textures - 1; i >= 0; i--) {
    gchar *scale_name = g_strdup_printf ("tex_scale%u", i);
    guint gl_target =
        gst_gl_texture_target_to_gl (convert->priv->from_texture_target);

    gl->ActiveTexture (GL_TEXTURE0 + i);
    gl->BindTexture (gl_target, convert->priv->in_tex[i]->tex_id);
    gl->TexParameteri (gl_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri (gl_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri (gl_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri (gl_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gst_gl_shader_set_uniform_2fv (convert->shader, scale_name, 1,
        convert->priv->in_tex[i]->tex_scaling);

    g_free (scale_name);
  }

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

  if (gl->BindVertexArray)
    gl->BindVertexArray (0);
  _unbind_buffer (convert);

  if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  /* we are done with the shader */
  gst_gl_context_clear_shader (context);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  if (!gst_gl_context_check_framebuffer_status (context))
    ret = FALSE;

  gst_gl_context_clear_framebuffer (context);

  return ret;
}
