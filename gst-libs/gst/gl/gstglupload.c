/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystree00@gmail.com>
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

#include <stdio.h>

#include "gl.h"
#include "gstglupload.h"

/**
 * SECTION:gstglupload
 * @short_description: an object that uploads to GL textures
 * @see_also: #GstGLDownload, #GstGLMemory
 *
 * #GstGLUpload is an object that uploads data from system memory into GL textures.
 *
 * A #GstGLUpload can be created with gst_gl_upload_new()
 */

#define USING_OPENGL(display) (gst_gl_display_get_gl_api (display) & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (gst_gl_display_get_gl_api (display) & GST_GL_API_OPENGL3)
#define USING_GLES(display) (gst_gl_display_get_gl_api (display) & GST_GL_API_GLES)
#define USING_GLES2(display) (gst_gl_display_get_gl_api (display) & GST_GL_API_GLES2)
#define USING_GLES3(display) (gst_gl_display_get_gl_api (display) & GST_GL_API_GLES3)

static void _do_upload (GstGLDisplay * display, GstGLUpload * upload);
static gboolean _do_upload_fill (GstGLDisplay * display, GstGLUpload * upload);
static gboolean _do_upload_make (GstGLDisplay * display, GstGLUpload * upload);
static void _init_upload (GstGLDisplay * display, GstGLUpload * upload);
static gboolean _init_upload_fbo (GstGLDisplay * display, GstGLUpload * upload);
static gboolean _gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);

#if GST_GL_HAVE_OPENGL
static gboolean _do_upload_draw_opengl (GstGLDisplay * display,
    GstGLUpload * upload);
#endif
#if GST_GL_HAVE_GLES2
static gboolean _do_upload_draw_gles2 (GstGLDisplay * display,
    GstGLUpload * upload);
#endif

/* *INDENT-OFF* */

#define YUV_TO_RGB_COEFFICIENTS \
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);\n" \
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);\n" \
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);\n" \
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);\n"

#if GST_GL_HAVE_OPENGL

static const char *frag_AYUV_opengl = {
      "#extension GL_ARB_texture_rectangle : enable\n"
      "uniform sampler2DRect tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n"
      "  float r,g,b;\n"
      "  vec3 yuv;\n"
      "  yuv  = texture2DRect(tex, gl_TexCoord[0].xy * tex_scale0).gba;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, rcoeff);\n"
      "  g = dot(yuv, gcoeff);\n"
      "  b = dot(yuv, bcoeff);\n"
      "  gl_FragColor=vec4(r,g,b,1.0);\n"
      "}"
};

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_opengl = {
      "#extension GL_ARB_texture_rectangle : enable\n"
      "uniform sampler2DRect Ytex,Utex,Vtex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n"
      "  float r,g,b;\n"
      "  vec3 yuv;\n"
      "  yuv.x=texture2DRect(Ytex, gl_TexCoord[0].xy * tex_scale0).r;\n"
      "  yuv.y=texture2DRect(Utex, gl_TexCoord[0].xy * tex_scale1).r;\n"
      "  yuv.z=texture2DRect(Vtex, gl_TexCoord[0].xy * tex_scale2).r;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, rcoeff);\n"
      "  g = dot(yuv, gcoeff);\n"
      "  b = dot(yuv, bcoeff);\n"
      "  gl_FragColor=vec4(r,g,b,1.0);\n"
      "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_opengl = {
      "#extension GL_ARB_texture_rectangle : enable\n"
      "uniform sampler2DRect Ytex,UVtex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n\n"
      "  float r,g,b;\n"
      "  vec3 yuv;\n"
      "  yuv.x = texture2DRect(Ytex, gl_TexCoord[0].xy * tex_scale0).r;\n"
      "  yuv.yz = texture2DRect(UVtex, gl_TexCoord[0].xy * tex_scale1).%c%c;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, rcoeff);\n"
      "  g = dot(yuv, gcoeff);\n"
      "  b = dot(yuv, bcoeff);\n"
      "  gl_FragColor=vec4(r,g,b,1.0);\n"
      "}"
};

/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_opengl = {
      "#extension GL_ARB_texture_rectangle : enable\n"
      "uniform sampler2DRect tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      "void main(void)\n"
      "{\n"
      " vec4 t = texture2DRect(tex, gl_TexCoord[0].xy);\n"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);\n"
      "}"
};

/* Direct fragments copy with stride-scaling */
static const char *frag_COPY_opengl = {
      "#extension GL_ARB_texture_rectangle : enable\n"
      "uniform sampler2DRect tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      "void main(void)\n"
      "{\n"
      " vec4 t = texture2DRect(tex, gl_TexCoord[0].xy);\n"
      " gl_FragColor = vec4(t.rgb, 1.0);\n"
      "}\n"
};

/* YUY2:r,g,a
   UYVY:a,b,r */
static const gchar *frag_YUY2_UYVY_opengl =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex, UVtex;\n"
    "uniform vec2 tex_scale0;\n"
    "uniform vec2 tex_scale1;\n"
    "uniform vec2 tex_scale2;\n"
    YUV_TO_RGB_COEFFICIENTS
    "void main(void) {\n"
    "  float fx, fy, y, u, v, r, g, b;\n"
    "  vec3 yuv;\n"
    "  yuv.x = texture2DRect(Ytex, gl_TexCoord[0].xy * tex_scale0).%c;\n"
    "  yuv.y = texture2DRect(UVtex, gl_TexCoord[0].xy * tex_scale1).%c;\n"
    "  yuv.z = texture2DRect(UVtex, gl_TexCoord[0].xy * tex_scale2).%c;\n"
    "  yuv += offset;\n"
    "  r = dot(yuv, rcoeff);\n"
    "  g = dot(yuv, gcoeff);\n"
    "  b = dot(yuv, bcoeff);\n"
    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

#define text_vertex_shader_opengl NULL
#endif

#if GST_GL_HAVE_GLES2
/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_gles2 = {
      "precision mediump float;\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      "void main(void)\n"
      "{\n"
      " vec4 t = texture2D(tex, v_texcoord);\n"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);\n"
      "}"
};

static const char *frag_AYUV_gles2 = {
      "precision mediump float;\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n"
      "  float r,g,b;\n"
      "  vec3 yuv;\n"
      "  yuv  = texture2D(tex,v_texcoord).gba;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, rcoeff);\n"
      "  g = dot(yuv, gcoeff);\n"
      "  b = dot(yuv, bcoeff);\n"
      "  gl_FragColor=vec4(r,g,b,1.0);\n"
      "}"
};

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_gles2 = {
      "precision mediump float;\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D Ytex,Utex,Vtex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n"
      "  float r,g,b;\n"
      "  vec3 yuv;\n"
      "  yuv.x=texture2D(Ytex,v_texcoord).r;\n"
      "  yuv.y=texture2D(Utex,v_texcoord).r;\n"
      "  yuv.z=texture2D(Vtex,v_texcoord).r;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, rcoeff);\n"
      "  g = dot(yuv, gcoeff);\n"
      "  b = dot(yuv, bcoeff);\n"
      "  gl_FragColor=vec4(r,g,b,1.0);\n"
      "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_gles2 = {
      "precision mediump float;\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D Ytex,UVtex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n"
      "  float r,g,b;\n"
      "  vec3 yuv;\n"
      "  yuv.x=texture2D(Ytex, v_texcoord).r;\n"
      "  yuv.yz=texture2D(UVtex, v_texcoord).%c%c;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, rcoeff);\n"
      "  g = dot(yuv, gcoeff);\n"
      "  b = dot(yuv, bcoeff);\n"
      "  gl_FragColor=vec4(r,g,b,1.0);\n"
      "}"
};

/* Direct fragments copy with stride-scaling */
static const char *frag_COPY_gles2 = {
      "precision mediump float;\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      "void main(void)\n"
      "{\n"
      " vec4 t = texture2D(tex, v_texcoord);\n"
      " gl_FragColor = vec4(t.rgb, 1.0);\n"
      "}"
};

/* YUY2:r,g,a
   UYVY:a,b,r */
static const gchar *frag_YUY2_UYVY_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D Ytex, UVtex;\n"
    YUV_TO_RGB_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 yuv;\n"
    "  float fx, fy, y, u, v, r, g, b;\n"
    "  fx = v_texcoord.x;\n"
    "  fy = v_texcoord.y;\n"
    "  yuv.x = texture2D(Ytex,vec2(fx,fy)).%c;\n"
    "  yuv.y = texture2D(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  yuv.z = texture2D(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  yuv+=offset;\n"
    "  r = dot(yuv, rcoeff);\n"
    "  g = dot(yuv, gcoeff);\n"
    "  b = dot(yuv, bcoeff);\n"
    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

static const gchar *text_vertex_shader_gles2 =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texcoord;   \n"
    "varying vec2 v_texcoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texcoord = a_texcoord;  \n"
    "}                            \n";
#endif

/* *INDENT-ON* */

struct _GstGLUploadPrivate
{
  int n_textures;
  gboolean result;

  const gchar *YUY2_UYVY;
  const gchar *PLANAR_YUV;
  const gchar *AYUV;
  const gchar *NV12_NV21;
  const gchar *REORDER;
  const gchar *COPY;
  const gchar *vert_shader;

    gboolean (*draw) (GstGLDisplay * display, GstGLUpload * download);
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_debug);
#define GST_CAT_DEFAULT gst_gl_upload_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "upload");

G_DEFINE_TYPE_WITH_CODE (GstGLUpload, gst_gl_upload, G_TYPE_OBJECT, DEBUG_INIT);
static void gst_gl_upload_finalize (GObject * object);

#define GST_GL_UPLOAD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_UPLOAD, GstGLUploadPrivate))

static void
gst_gl_upload_class_init (GstGLUploadClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLUploadPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_upload_finalize;
}

static void
gst_gl_upload_init (GstGLUpload * upload)
{
  upload->priv = GST_GL_UPLOAD_GET_PRIVATE (upload);

  upload->display = NULL;

  g_mutex_init (&upload->lock);

  upload->fbo = 0;
  upload->depth_buffer = 0;
  upload->out_texture = 0;
  upload->shader = NULL;

  upload->shader_attr_position_loc = 0;
  upload->shader_attr_texture_loc = 0;

  gst_video_info_init (&upload->info);
}

/**
 * gst_gl_upload_new:
 * @display: a #GstGLDisplay
 *
 * Returns: a new #GstGLUpload object
 */
GstGLUpload *
gst_gl_upload_new (GstGLDisplay * display)
{
  GstGLUpload *upload;
  GstGLUploadPrivate *priv;

  upload = g_object_new (GST_TYPE_GL_UPLOAD, NULL);

  upload->display = gst_object_ref (display);
  priv = upload->priv;

  g_mutex_init (&upload->lock);

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (display)) {
    priv->YUY2_UYVY = frag_YUY2_UYVY_opengl;
    priv->PLANAR_YUV = frag_PLANAR_YUV_opengl;
    priv->AYUV = frag_AYUV_opengl;
    priv->REORDER = frag_REORDER_opengl;
    priv->COPY = frag_COPY_opengl;
    priv->NV12_NV21 = frag_NV12_NV21_opengl;
    priv->vert_shader = text_vertex_shader_opengl;
    priv->draw = _do_upload_draw_opengl;
  }
#endif
#if GST_GL_HAVE_GLES2
  if (USING_GLES2 (display)) {
    priv->YUY2_UYVY = frag_YUY2_UYVY_gles2;
    priv->PLANAR_YUV = frag_PLANAR_YUV_gles2;
    priv->AYUV = frag_AYUV_gles2;
    priv->REORDER = frag_REORDER_gles2;
    priv->COPY = frag_COPY_gles2;
    priv->NV12_NV21 = frag_NV12_NV21_gles2;
    priv->vert_shader = text_vertex_shader_gles2;
    priv->draw = _do_upload_draw_gles2;
  }
#endif

  return upload;
}

static void
gst_gl_upload_finalize (GObject * object)
{
  GstGLUpload *upload;
  guint i;

  upload = GST_GL_UPLOAD (object);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->in_texture[i]) {
      gst_gl_display_del_texture (upload->display, &upload->in_texture[i]);
      upload->in_texture[i] = 0;
    }
  }
  if (upload->out_texture) {
    gst_gl_display_del_texture (upload->display, &upload->out_texture);
    upload->out_texture = 0;
  }
  if (upload->fbo || upload->depth_buffer) {
    gst_gl_display_del_fbo (upload->display, upload->fbo, upload->depth_buffer);
    upload->fbo = 0;
    upload->depth_buffer = 0;
  }
  if (upload->shader) {
    gst_object_unref (upload->shader);
    upload->shader = NULL;
  }

  if (upload->display) {
    gst_object_unref (upload->display);
    upload->display = NULL;
  }

  G_OBJECT_CLASS (gst_gl_upload_parent_class)->finalize (object);
}

/**
 * gst_gl_upload_init_format:
 * @upload: a #GstGLUpload
 * @v_format: a #GstVideoFormat
 * @in_width: the width of the data to upload
 * @in_height: the height of the data to upload
 * @out_width: the width to upload to
 * @out_height: the height to upload to
 *
 * Initializes @upload with the information required for upload.
 *
 * Returns: whether the initialization was successful
 */
gboolean
gst_gl_upload_init_format (GstGLUpload * upload, GstVideoFormat v_format,
    guint in_width, guint in_height, guint out_width, guint out_height)
{
  GstVideoInfo info;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (in_width > 0 && in_height > 0, FALSE);
  g_return_val_if_fail (out_width > 0 && out_height > 0, FALSE);

  g_mutex_lock (&upload->lock);

  if (upload->initted) {
    g_mutex_unlock (&upload->lock);
    return FALSE;
  } else {
    upload->initted = TRUE;
  }

  gst_video_info_set_format (&info, v_format, out_width, out_height);

  upload->info = info;
  upload->in_width = in_width;
  upload->in_height = in_height;

  gst_gl_display_thread_add (upload->display,
      (GstGLDisplayThreadFunc) _init_upload, upload);

  g_mutex_unlock (&upload->lock);

  return upload->priv->result;
}

/**
 * gst_gl_upload_perform_with_memory:
 * @upload: a #GstGLUpload
 * @gl_mem: a #GstGLMemory
 *
 * Uploads the texture in @gl_mem
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_memory (GstGLUpload * upload, GstGLMemory * gl_mem)
{
  gpointer data[GST_VIDEO_MAX_PLANES];
  guint i;
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_UPLOAD_INITTED))
    return FALSE;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD))
    return FALSE;

  g_mutex_lock (&upload->lock);

  upload->in_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  upload->in_height = GST_VIDEO_INFO_HEIGHT (&upload->info);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&upload->info, i);
  }

  ret =
      _gst_gl_upload_perform_with_data_unlocked (upload, gl_mem->tex_id, data);

  if (ret)
    GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);

  g_mutex_unlock (&upload->lock);

  return ret;
}

/**
 * gst_gl_upload_perform_with_data:
 * @upload: a #GstGLUpload
 * @texture_id: the texture id to download
 * @data: where the downloaded data should go
 *
 * Uploads @data into @texture_id. @data size and format is specified by
 * the #GstVideoFormat passed to gst_gl_upload_init_format() 
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_data (GstGLUpload * upload, GLuint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  g_mutex_lock (&upload->lock);

  ret = _gst_gl_upload_perform_with_data_unlocked (upload, texture_id, data);

  g_mutex_unlock (&upload->lock);

  return ret;
}

static gboolean
_gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  guint i;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (texture_id > 0, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&upload->info) !=
      GST_VIDEO_FORMAT_UNKNOWN
      && GST_VIDEO_INFO_FORMAT (&upload->info) != GST_VIDEO_FORMAT_ENCODED,
      FALSE);

  upload->out_texture = texture_id;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    upload->data[i] = data[i];
  }

  gst_gl_display_thread_add (upload->display,
      (GstGLDisplayThreadFunc) _do_upload, upload);

  return TRUE;
}

static gboolean
_create_shader (GstGLDisplay * display, const gchar * vertex_src,
    const gchar * fragment_src, GstGLShader ** out_shader)
{
  GstGLShader *shader;
  GError *error = NULL;

  g_return_val_if_fail (vertex_src != NULL || fragment_src != NULL, FALSE);

  shader = gst_gl_shader_new (display);

  if (vertex_src)
    gst_gl_shader_set_vertex_source (shader, vertex_src);
  if (fragment_src)
    gst_gl_shader_set_fragment_source (shader, fragment_src);

  if (!gst_gl_shader_compile (shader, &error)) {
    gst_gl_display_set_error (display, "%s", error->message);
    g_error_free (error);
    gst_gl_display_clear_shader (display);
    gst_object_unref (shader);
    return FALSE;
  }

  *out_shader = shader;
  return TRUE;
}

/* Called in the gl thread */
void
_init_upload (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  gchar *frag_prog = NULL;
  gboolean free_frag_prog, res;

  gl = display->gl_vtable;

  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  GST_INFO ("Initializing texture upload for format:%s",
      gst_video_format_to_string (v_format));

  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    gst_gl_display_set_error (display,
        "Cannot upload YUV formats without OpenGL shaders");
    goto error;
  }

  _init_upload_fbo (display, upload);

  switch (v_format) {
    case GST_VIDEO_FORMAT_AYUV:
      frag_prog = (gchar *) upload->priv->AYUV;
      free_frag_prog = FALSE;
      upload->priv->n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      frag_prog = (gchar *) upload->priv->PLANAR_YUV;
      free_frag_prog = FALSE;
      upload->priv->n_textures = 3;
      break;
    case GST_VIDEO_FORMAT_NV12:
      frag_prog = g_strdup_printf (upload->priv->NV12_NV21, 'r', 'a');
      free_frag_prog = TRUE;
      upload->priv->n_textures = 2;
      break;
    case GST_VIDEO_FORMAT_NV21:
      frag_prog = g_strdup_printf (upload->priv->NV12_NV21, 'a', 'r');
      free_frag_prog = TRUE;
      upload->priv->n_textures = 2;
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      frag_prog = g_strdup_printf (upload->priv->REORDER, 'b', 'g', 'r');
      free_frag_prog = TRUE;
      upload->priv->n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      frag_prog = g_strdup_printf (upload->priv->REORDER, 'g', 'b', 'a');
      free_frag_prog = TRUE;
      upload->priv->n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      frag_prog = g_strdup_printf (upload->priv->REORDER, 'a', 'b', 'g');
      free_frag_prog = TRUE;
      upload->priv->n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB16:
      frag_prog = (gchar *) upload->priv->COPY;
      free_frag_prog = FALSE;
      upload->priv->n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      frag_prog = g_strdup_printf (upload->priv->YUY2_UYVY, 'r', 'g', 'a');
      free_frag_prog = TRUE;
      upload->priv->n_textures = 2;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      if (USING_GLES2 (display)) {
        frag_prog = g_strdup_printf (upload->priv->YUY2_UYVY, 'a', 'r', 'b');
      } else {
        frag_prog = g_strdup_printf (upload->priv->YUY2_UYVY, 'a', 'b', 'r');
      }
      free_frag_prog = TRUE;
      upload->priv->n_textures = 2;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  res =
      _create_shader (display, upload->priv->vert_shader, frag_prog,
      &upload->shader);
  if (free_frag_prog)
    g_free (frag_prog);
  frag_prog = NULL;
  if (!res)
    goto error;

  if (USING_GLES2 (display)) {
    upload->shader_attr_position_loc =
        gst_gl_shader_get_attribute_location (upload->shader, "a_position");
    upload->shader_attr_texture_loc =
        gst_gl_shader_get_attribute_location (upload->shader, "a_texcoord");
  }

  if (!_do_upload_make (display, upload))
    goto error;

  upload->priv->result = TRUE;
  return;

error:
  upload->priv->result = FALSE;
}


/* called by _init_upload (in the gl thread) */
gboolean
_init_upload_fbo (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  guint out_width, out_height;
  GLuint fake_texture = 0;      /* a FBO must hava texture to init */

  gl = display->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);

  if (!gl->GenFramebuffers) {
    /* turn off the pipeline because Frame buffer object is a not present */
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object supported: no");
    return FALSE;
  }

  GST_INFO ("Context, EXT_framebuffer_object supported: yes");

  /* setup FBO */
  gl->GenFramebuffers (1, &upload->fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, upload->fbo);

  /* setup the render buffer for depth */
  gl->GenRenderbuffers (1, &upload->depth_buffer);
  gl->BindRenderbuffer (GL_RENDERBUFFER, upload->depth_buffer);
  if (USING_OPENGL (display)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
        out_width, out_height);
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
        out_width, out_height);
  }
  if (USING_GLES2 (display)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
        out_width, out_height);
  }

  /* a fake texture is attached to the upload FBO (cannot init without it) */
  gl->GenTextures (1, &fake_texture);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, out_width, out_height,
      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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
      GL_RENDERBUFFER, upload->depth_buffer);

  if (USING_OPENGL (display)) {
    gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, upload->depth_buffer);
  }

  if (!gst_gl_display_check_framebuffer_status (display)) {
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");
    return FALSE;
  }

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteTextures (1, &fake_texture);

  return TRUE;
}

/* Called by the idle function in the gl thread */
void
_do_upload (GstGLDisplay * display, GstGLUpload * upload)
{
  guint in_width, in_height, out_width, out_height;

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  in_width = upload->in_width;
  in_height = upload->in_height;

  GST_TRACE ("uploading to texture:%u dimensions:%ux%u, "
      "from textures:%u,%u,%u dimensions:%ux%u", upload->out_texture,
      out_width, out_height, upload->in_texture[0], upload->in_texture[1],
      upload->in_texture[2], in_width, in_height);

  if (!_do_upload_fill (display, upload))
    goto error;

  if (!upload->priv->draw (display, upload))
    goto error;

  upload->priv->result = TRUE;
  return;

error:
  {
    upload->priv->result = FALSE;
    return;
  }
}

struct TexData
{
  gint internal_format, format, type, width, height;
};

/* called by gst_gl_display_thread_do_upload (in the gl thread) */
gboolean
_do_upload_make (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint in_width, in_height;
  struct TexData tex[GST_VIDEO_MAX_PLANES];
  guint i;

  gl = display->gl_vtable;

  in_width = upload->in_width;
  in_height = upload->in_height;
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      tex[0].internal_format = GL_RGBA;
      tex[0].format = GL_RGBA;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      tex[0].internal_format = GL_RGB;
      tex[0].format = GL_RGB;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      tex[0].internal_format = GL_RGBA;
      tex[0].format = GL_BGRA;
      tex[0].type = GL_UNSIGNED_INT_8_8_8_8;
      tex[0].width = in_width;
      tex[0].height = in_height;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      tex[0].internal_format = GL_LUMINANCE_ALPHA;
      tex[0].format = GL_LUMINANCE_ALPHA;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_RGBA8;
      tex[1].format = GL_BGRA;
      tex[1].type = GL_UNSIGNED_INT_8_8_8_8;
      tex[1].width = in_width;
      tex[1].height = in_height;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      tex[0].internal_format = GL_LUMINANCE_ALPHA;
      tex[0].format = GL_LUMINANCE_ALPHA;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_RGBA8;
      tex[1].format = GL_BGRA;
      tex[1].type = GL_UNSIGNED_INT_8_8_8_8_REV;
      tex[1].width = in_width;
      tex[1].height = in_height;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      tex[0].internal_format = GL_LUMINANCE;
      tex[0].format = GL_LUMINANCE;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_LUMINANCE_ALPHA;
      tex[1].format = GL_LUMINANCE_ALPHA;
      tex[1].type = GL_UNSIGNED_BYTE;
      tex[1].width = in_width / 2;
      tex[1].height = in_height / 2;
      break;
    case GST_VIDEO_FORMAT_Y444:
      tex[0].internal_format = GL_LUMINANCE;
      tex[0].format = GL_LUMINANCE;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_LUMINANCE;
      tex[1].format = GL_LUMINANCE;
      tex[1].type = GL_UNSIGNED_BYTE;
      tex[1].width = in_width;
      tex[1].height = in_height;
      tex[2].internal_format = GL_LUMINANCE;
      tex[2].format = GL_LUMINANCE;
      tex[2].type = GL_UNSIGNED_BYTE;
      tex[2].width = in_width;
      tex[2].height = in_height;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      tex[0].internal_format = GL_LUMINANCE;
      tex[0].format = GL_LUMINANCE;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_LUMINANCE;
      tex[1].format = GL_LUMINANCE;
      tex[1].type = GL_UNSIGNED_BYTE;
      tex[1].width = GST_ROUND_UP_2 (in_width) / 2;
      tex[1].height = GST_ROUND_UP_2 (in_height) / 2;
      tex[2].internal_format = GL_LUMINANCE;
      tex[2].format = GL_LUMINANCE;
      tex[2].type = GL_UNSIGNED_BYTE;
      tex[2].width = GST_ROUND_UP_2 (in_width) / 2;
      tex[2].height = GST_ROUND_UP_2 (in_height) / 2;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      tex[0].internal_format = GL_LUMINANCE;
      tex[0].format = GL_LUMINANCE;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_LUMINANCE;
      tex[1].format = GL_LUMINANCE;
      tex[1].type = GL_UNSIGNED_BYTE;
      tex[1].width = GST_ROUND_UP_2 (in_width) / 2;
      tex[1].height = in_height;
      tex[2].internal_format = GL_LUMINANCE;
      tex[2].format = GL_LUMINANCE;
      tex[2].type = GL_UNSIGNED_BYTE;
      tex[2].width = GST_ROUND_UP_2 (in_width) / 2;
      tex[2].height = in_height;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      tex[0].internal_format = GL_LUMINANCE;
      tex[0].format = GL_LUMINANCE;
      tex[0].type = GL_UNSIGNED_BYTE;
      tex[0].width = in_width;
      tex[0].height = in_height;
      tex[1].internal_format = GL_LUMINANCE;
      tex[1].format = GL_LUMINANCE;
      tex[1].type = GL_UNSIGNED_BYTE;
      tex[1].width = GST_ROUND_UP_4 (in_width) / 4;
      tex[1].height = in_height;
      tex[2].internal_format = GL_LUMINANCE;
      tex[2].format = GL_LUMINANCE;
      tex[2].type = GL_UNSIGNED_BYTE;
      tex[2].width = GST_ROUND_UP_4 (in_width) / 4;
      tex[2].height = in_height;
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  for (i = 0; i < upload->priv->n_textures; i++) {
    gl->GenTextures (1, &upload->in_texture[i]);
    gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[i]);

    gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, tex[i].internal_format,
        tex[i].width, tex[i].height, 0, tex[i].format, tex[i].type, NULL);
  }

  return TRUE;
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
gboolean
_do_upload_fill (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint in_width, in_height;

  gl = display->gl_vtable;

  in_width = upload->in_width;
  in_height = upload->in_height;
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGB, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGBA, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
          in_height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, in_height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
          in_height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, in_height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          in_width / 2, in_height / 2,
          GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[1]);
      break;
    case GST_VIDEO_FORMAT_I420:
    {
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_YV12:        /* same as I420 except plane 1+2 swapped */
    {
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_Y444:
    {
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          in_width, in_height, GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          in_width, in_height, GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_Y42B:
    {
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_Y41B:
    {
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_4 (in_width) / 4, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_4 (in_width) / 4, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  /* make sure no texture is in use in our opengl context 
   * in case we want to use the upload texture in an other opengl context
   */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  return TRUE;
}

#if GST_GL_HAVE_OPENGL
/* called by _do_upload (in the gl thread) */
static gboolean
_do_upload_draw_opengl (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;
  guint in_width = upload->in_width;
  guint in_height = upload->in_height;
  char *texnames[GST_VIDEO_MAX_PLANES];
  gint i;
  gfloat tex_scaling[6] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

  GLfloat verts[8] = { 1.0f, -1.0f,
    -1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f
  };
  GLfloat texcoords[8] = { in_width, 0,
    0, 0,
    0, in_height,
    in_width, in_height
  };

  gl = display->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  gl->BindFramebuffer (GL_FRAMEBUFFER, upload->fbo);

  /* setup a texture to render to */
  gl->Enable (GL_TEXTURE_RECTANGLE_ARB);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_RECTANGLE_ARB, upload->out_texture, 0);

  gst_gl_display_clear_shader (display);

  gl->PushAttrib (GL_VIEWPORT_BIT);

  gl->MatrixMode (GL_PROJECTION);
  gl->PushMatrix ();
  gl->LoadIdentity ();
  gluOrtho2D (0.0, out_width, 0.0, out_height);

  gl->MatrixMode (GL_MODELVIEW);
  gl->PushMatrix ();
  gl->LoadIdentity ();

  gl->Viewport (0, 0, out_width, out_height);

  gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      tex_scaling[2] = tex_scaling[3] = 0.5;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      tex_scaling[2] = tex_scaling[4] = 0.5;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      if (v_format == GST_VIDEO_FORMAT_I420
          || v_format == GST_VIDEO_FORMAT_YV12)
        tex_scaling[2] = tex_scaling[3] = tex_scaling[4] = tex_scaling[5] = 0.5;
      else if (v_format == GST_VIDEO_FORMAT_Y42B)
        tex_scaling[2] = tex_scaling[4] = 0.5;
      else if (v_format == GST_VIDEO_FORMAT_Y41B)
        tex_scaling[2] = tex_scaling[4] = 0.25;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      texnames[0] = "tex";
      break;

    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  gst_gl_shader_use (upload->shader);
  gst_gl_shader_set_uniform_2fv (upload->shader, "tex_scale0", 1,
      &tex_scaling[0]);
  gst_gl_shader_set_uniform_2fv (upload->shader, "tex_scale1", 1,
      &tex_scaling[2]);
  gst_gl_shader_set_uniform_2fv (upload->shader, "tex_scale2", 1,
      &tex_scaling[4]);

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gl->Enable (GL_TEXTURE_RECTANGLE_ARB);

  for (i = upload->priv->n_textures - 1; i >= 0; i--) {
    gl->ActiveTexture (GL_TEXTURE0 + i);
    gst_gl_shader_set_uniform_1i (upload->shader, texnames[i], i);

    gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[i]);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
        GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
        GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE);
  }

  gl->EnableClientState (GL_VERTEX_ARRAY);
  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->VertexPointer (2, GL_FLOAT, 0, &verts);
  gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords);

  gl->DrawArrays (GL_TRIANGLE_FAN, 0, 4);

  gl->DisableClientState (GL_VERTEX_ARRAY);
  gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->DrawBuffer (GL_NONE);

  /* we are done with the shader */
  gst_gl_display_clear_shader (display);

  gl->Disable (GL_TEXTURE_RECTANGLE_ARB);

  gl->MatrixMode (GL_PROJECTION);
  gl->PopMatrix ();
  gl->MatrixMode (GL_MODELVIEW);
  gl->PopMatrix ();
  gl->PopAttrib ();

  gst_gl_display_check_framebuffer_status (display);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  return TRUE;
}
#endif

#if GST_GL_HAVE_GLES2
static gboolean
_do_upload_draw_gles2 (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;
  char *texnames[GST_VIDEO_MAX_PLANES];
  gint i;
  gfloat tex_scaling[6] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

  GLint viewport_dim[4];

  const GLfloat vVertices[] = { 1.0f, -1.0f, 0.0f,
    1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
    0.0f, 0.0f,
    -1.0f, 1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f, 0.0f,
    1.0f, 1.0f
  };

  GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

  gl = display->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  gl->BindFramebuffer (GL_FRAMEBUFFER, upload->fbo);

  /* setup a texture to render to */
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_RECTANGLE_ARB, upload->out_texture, 0);

  gst_gl_display_clear_shader (display);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, out_width, out_height);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      tex_scaling[2] = tex_scaling[3] = 0.5;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      tex_scaling[2] = tex_scaling[4] = 0.5;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      if (v_format == GST_VIDEO_FORMAT_I420
          || v_format == GST_VIDEO_FORMAT_YV12)
        tex_scaling[2] = tex_scaling[3] = tex_scaling[4] = tex_scaling[5] = 0.5;
      else if (v_format == GST_VIDEO_FORMAT_Y42B)
        tex_scaling[2] = tex_scaling[4] = 0.5;
      else if (v_format == GST_VIDEO_FORMAT_Y41B)
        tex_scaling[2] = tex_scaling[4] = 0.25;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      texnames[0] = "tex";
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  gst_gl_shader_use (upload->shader);
  gst_gl_shader_set_uniform_2fv (upload->shader, "tex_scale0", 1,
      &tex_scaling[0]);
  gst_gl_shader_set_uniform_2fv (upload->shader, "tex_scale1", 1,
      &tex_scaling[2]);
  gst_gl_shader_set_uniform_2fv (upload->shader, "tex_scale2", 1,
      &tex_scaling[4]);

  gl->VertexAttribPointer (upload->shader_attr_position_loc, 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
  gl->VertexAttribPointer (upload->shader_attr_texture_loc, 2,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

  gl->EnableVertexAttribArray (upload->shader_attr_position_loc);
  gl->EnableVertexAttribArray (upload->shader_attr_texture_loc);

  for (i = upload->priv->n_textures - 1; i >= 0; i--) {
    gl->ActiveTexture (GL_TEXTURE0 + i);
    gst_gl_shader_set_uniform_1i (upload->shader, texnames[i], i);

    gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[i]);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
        GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
        GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE);
    gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE);
  }

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  /* we are done with the shader */
  gst_gl_display_clear_shader (display);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  gst_gl_display_check_framebuffer_status (display);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  return TRUE;
}
#endif
