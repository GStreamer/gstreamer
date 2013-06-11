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

#include "gstglupload.h"
#include "gstglmemory.h"

/**
 * SECTION:gstglupload
 * @short_description: an object that uploads to GL textures
 * @see_also: #GstGLDownload, #GstGLMemory
 *
 * #GstGLUpload is an object that uploads data from system memory into GL textures.
 *
 * A #GstGLUpload can be created with gst_gl_upload_new() or found with
 * gst_gl_display_find_upload().
 *
 * All of the _thread() variants should only be called within the GL thread
 * they don't try to take a lock on the associated #GstGLDisplay and
 * don't dispatch to the GL thread. Rather they run the required code in the
 * calling thread.
 */

#define USING_OPENGL(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_OPENGL3)
#define USING_GLES(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_GLES)
#define USING_GLES2(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_GLES2)
#define USING_GLES3(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_GLES3)

static void _do_upload (GstGLDisplay * display, GstGLUpload * upload);
static void _do_upload_fill (GstGLDisplay * display, GstGLUpload * upload);
static void _do_upload_make (GstGLDisplay * display, GstGLUpload * upload);
static void _init_upload (GstGLDisplay * display, GstGLUpload * upload);
static void _init_upload_fbo (GstGLDisplay * display, GstGLUpload * upload);
static gboolean gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);
static gboolean gst_gl_upload_perform_with_data_unlocked_thread (GstGLUpload *
    upload, GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);

#if GST_GL_HAVE_OPENGL
static void _do_upload_draw_opengl (GstGLDisplay * display,
    GstGLUpload * upload);
#endif
#if GST_GL_HAVE_GLES2
static void _do_upload_draw_gles2 (GstGLDisplay * display,
    GstGLUpload * upload);
#endif

/* *INDENT-OFF* */

#if GST_GL_HAVE_OPENGL
/* YUY2:r,g,a
   UYVY:a,b,r */
static gchar *text_shader_YUY2_UYVY_opengl =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex, UVtex;\n"
    "void main(void) {\n"
    "  float fx, fy, y, u, v, r, g, b;\n"
    "  fx = gl_TexCoord[0].x;\n"
    "  fy = gl_TexCoord[0].y;\n"
    "  y = texture2DRect(Ytex,vec2(fx,fy)).%c;\n"
    "  u = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  v = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  y=1.164*(y-0.0627);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r = y+1.5958*v;\n"
    "  g = y-0.39173*u-0.81290*v;\n"
    "  b = y+2.017*u;\n"
    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

/* ATI: "*0.5", ""
   normal: "", "*0.5" */
static gchar *text_shader_I420_YV12_opengl =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex,Utex,Vtex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy = gl_TexCoord[0].xy;\n"
    "  y=texture2DRect(Ytex,nxy%s).r;\n"
    "  u=texture2DRect(Utex,nxy%s).r;\n"
    "  v=texture2DRect(Vtex,nxy*0.5).r;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

static gchar *text_shader_AYUV_opengl =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  y=texture2DRect(tex,nxy).r;\n"
    "  u=texture2DRect(tex,nxy).g;\n"
    "  v=texture2DRect(tex,nxy).b;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

#define text_vertex_shader_opengl NULL
#endif

#if GST_GL_HAVE_GLES2
/* YUY2:r,g,a
   UYVY:a,b,r */
static gchar *text_shader_YUY2_UYVY_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D Ytex, UVtex;\n"
    "void main(void) {\n"
    "  float fx, fy, y, u, v, r, g, b;\n"
    "  fx = v_texCoord.x;\n"
    "  fy = v_texCoord.y;\n"
    "  y = texture2D(Ytex,vec2(fx,fy)).%c;\n"
    "  u = texture2D(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  v = texture2D(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  y=1.164*(y-0.0627);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r = y+1.5958*v;\n"
    "  g = y-0.39173*u-0.81290*v;\n"
    "  b = y+2.017*u;\n"
    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

static gchar *text_shader_I420_YV12_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D Ytex,Utex,Vtex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy = v_texCoord.xy;\n"
    "  y=texture2D(Ytex,nxy).r;\n"
    "  u=texture2D(Utex,nxy).r;\n"
    "  v=texture2D(Vtex,nxy).r;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

static gchar *text_shader_AYUV_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D tex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy = v_texCoord.xy;\n"
    "  y=texture2D(tex,nxy).g;\n"
    "  u=texture2D(tex,nxy).b;\n"
    "  v=texture2D(tex,nxy).a;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

static gchar *text_vertex_shader_gles2 =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texCoord = a_texCoord;  \n"
    "}                            \n";
#endif

/* *INDENT-ON* */

struct _GstGLUploadPrivate
{
  const gchar *YUY2_UYVY;
  const gchar *I420_YV12;
  const gchar *AYUV;
  const gchar *vert_shader;

  void (*draw) (GstGLDisplay * display, GstGLUpload * download);
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

  upload->display = g_object_ref (display);
  priv = upload->priv;

  g_mutex_init (&upload->lock);

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (display)) {
    priv->YUY2_UYVY = text_shader_YUY2_UYVY_opengl;
    priv->I420_YV12 = text_shader_I420_YV12_opengl;
    priv->AYUV = text_shader_AYUV_opengl;
    priv->vert_shader = text_vertex_shader_opengl;
    priv->draw = _do_upload_draw_opengl;
  }
#endif
#if GST_GL_HAVE_GLES2
  if (USING_GLES2 (display)) {
    priv->YUY2_UYVY = text_shader_YUY2_UYVY_gles2;
    priv->I420_YV12 = text_shader_I420_YV12_gles2;
    priv->AYUV = text_shader_AYUV_gles2;
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
    g_object_unref (G_OBJECT (upload->shader));
    upload->shader = NULL;
  }

  if (upload->display) {
    g_object_unref (G_OBJECT (upload->display));
    upload->display = NULL;
  }
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

  return TRUE;
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
gst_gl_upload_init_format_thread (GstGLUpload * upload, GstVideoFormat v_format,
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

  _init_upload (upload->display, upload);

  g_mutex_unlock (&upload->lock);

  return TRUE;
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

  ret = gst_gl_upload_perform_with_data_unlocked (upload, gl_mem->tex_id, data);

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

  ret = gst_gl_upload_perform_with_data_unlocked (upload, texture_id, data);

  g_mutex_unlock (&upload->lock);

  return ret;
}

static inline gboolean
_perform_with_data_unlocked_pre (GstGLUpload * upload,
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

  return TRUE;
}

static gboolean
gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  if (!_perform_with_data_unlocked_pre (upload, texture_id, data))
    return FALSE;

  gst_gl_display_thread_add (upload->display,
      (GstGLDisplayThreadFunc) _do_upload, upload);

  return TRUE;
}

/**
 * gst_gl_upload_perform_with_memory_thread:
 * @upload: a #GstGLUpload
 * @gl_mem: a #GstGLMemory
 *
 * Uploads the texture in @gl_mem
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_memory_thread (GstGLUpload * upload,
    GstGLMemory * gl_mem)
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

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&upload->info, i);
  }

  ret =
      gst_gl_upload_perform_with_data_unlocked_thread (upload, gl_mem->tex_id,
      data);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);

  g_mutex_unlock (&upload->lock);

  return ret;
}

/**
 * gst_gl_upload_perform_with_data_thread:
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
gst_gl_upload_perform_with_data_thread (GstGLUpload * upload, GLuint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  g_mutex_lock (&upload->lock);

  ret =
      gst_gl_upload_perform_with_data_unlocked_thread (upload, texture_id,
      data);

  g_mutex_unlock (&upload->lock);

  return ret;
}

static gboolean
gst_gl_upload_perform_with_data_unlocked_thread (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  if (!_perform_with_data_unlocked_pre (upload, texture_id, data))
    return FALSE;

  _do_upload (upload->display, upload);

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
    g_object_unref (G_OBJECT (shader));
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
  guint in_width, in_height, out_width, out_height;

  gl = display->gl_vtable;

  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);
  in_width = upload->in_width;
  in_height = upload->in_height;
  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);

  GST_INFO ("Initializing texture upload for format:%s",
      gst_video_format_to_string (v_format));

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
      if (in_width != out_width || in_height != out_height)
        _init_upload_fbo (display, upload);
      /* color space conversion is not needed */
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
    {
      /* check if fragment shader is available, then load them */
      /* shouldn't we require ARB_shading_language_100? --Filippo */
      if (gl->CreateProgramObject || gl->CreateProgram) {

        GST_INFO ("We have OpenGL shaders");

        _init_upload_fbo (display, upload);

        switch (v_format) {
          case GST_VIDEO_FORMAT_YUY2:
          {
            gchar text_shader_YUY2[2048];
            sprintf (text_shader_YUY2, upload->priv->YUY2_UYVY, 'r', 'g', 'a');

            if (_create_shader (display, upload->priv->vert_shader,
                    text_shader_YUY2, &upload->shader)) {
              if (USING_GLES2 (display)) {
                upload->shader_attr_position_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_position");
                upload->shader_attr_texture_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_texCoord");
              }
            }
          }
            break;
          case GST_VIDEO_FORMAT_UYVY:
          {
            gchar text_shader_UYVY[2048];
#if GST_GL_HAVE_OPENGL
            if (USING_OPENGL (display)) {
              sprintf (text_shader_UYVY, upload->priv->YUY2_UYVY,
                  'a', 'b', 'r');
            }
#endif
#if GST_GL_HAVE_GLES2
            if (USING_GLES2 (display)) {
              sprintf (text_shader_UYVY, upload->priv->YUY2_UYVY,
                  'a', 'r', 'b');
            }
#endif

            if (_create_shader (display, upload->priv->vert_shader,
                    text_shader_UYVY, &upload->shader)) {
              if (USING_GLES2 (display)) {
                upload->shader_attr_position_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_position");
                upload->shader_attr_texture_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_texCoord");
              }
            }
          }
            break;
          case GST_VIDEO_FORMAT_I420:
          case GST_VIDEO_FORMAT_YV12:
          {
            gchar text_shader_I420_YV12[2048];
#if GST_GL_HAVE_OPENGL
            if (USING_OPENGL (display)) {
              if ((g_ascii_strncasecmp ("ATI",
                          (gchar *) glGetString (GL_VENDOR), 3) == 0)
                  && (g_ascii_strncasecmp ("ATI Mobility Radeon HD",
                          (gchar *) glGetString (GL_RENDERER), 22) != 0)
                  && (g_ascii_strncasecmp ("ATI Radeon HD",
                          (gchar *) glGetString (GL_RENDERER), 13) != 0))
                sprintf (text_shader_I420_YV12, upload->priv->I420_YV12, "*0.5",
                    "");
              else
                sprintf (text_shader_I420_YV12, upload->priv->I420_YV12, "",
                    "*0.5");
            }
#endif
#if GST_GL_HAVE_GLES2
            if (USING_GLES2 (display))
              g_strlcpy (text_shader_I420_YV12, upload->priv->I420_YV12, 2048);
#endif

            if (_create_shader (display, upload->priv->vert_shader,
                    text_shader_I420_YV12, &upload->shader)) {
              if (USING_GLES2 (display)) {
                upload->shader_attr_position_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_position");
                upload->shader_attr_texture_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_texCoord");
              }
            }
          }
            break;
          case GST_VIDEO_FORMAT_AYUV:
          {
            if (_create_shader (display, upload->priv->vert_shader,
                    upload->priv->AYUV, &upload->shader)) {
              if (USING_GLES2 (display)) {
                upload->shader_attr_position_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_position");
                upload->shader_attr_texture_loc =
                    gst_gl_shader_get_attribute_location
                    (upload->shader, "a_texCoord");
              }
            }
          }
            break;
          default:
            gst_gl_display_set_error (display,
                "Unsupported upload video format %s",
                gst_video_format_to_string (v_format));
            break;
        }
        /* check if YCBCR MESA is available */
#if 0
      } else if (GLEW_MESA_ycbcr_texture) {
        /* GLSL and Color Matrix are not available on your drivers,
         * switch to YCBCR MESA
         */
        GST_INFO ("Context, ARB_fragment_shader supported: no");
        GST_INFO ("Context, GLEW_MESA_ycbcr_texture supported: yes");

        display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MESA;

        switch (v_format) {
          case GST_VIDEO_FORMAT_YUY2:
          case GST_VIDEO_FORMAT_UYVY:
            /* color space conversion is not needed */
            break;
          case GST_VIDEO_FORMAT_I420:
          case GST_VIDEO_FORMAT_YV12:
          case GST_VIDEO_FORMAT_AYUV:
            /* MESA only supports YUY2 and UYVY */
            gst_gl_display_set_error (display,
                "Your MESA version only supports YUY2 and UYVY (GLSL is required for others yuv formats)");
            break;
          default:
            gst_gl_display_set_error (display,
                "Unsupported upload video format %d", v_format);
            break;
        }
      }
      /* check if color matrix is available (not supported) */
      else if (GLEW_ARB_imaging) {
        /* GLSL is not available on your drivers, switch to Color Matrix */
        GST_INFO ("Context, ARB_fragment_shader supported: no");
        GST_INFO ("Context, GLEW_MESA_ycbcr_texture supported: no");
        GST_INFO ("Context, GLEW_ARB_imaging supported: yes");

        display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MATRIX;

        gst_gl_display_set_error (display,
            "Colorspace conversion using Color Matrix is not yet supported");
#endif
      } else {
        /* colorspace conversion is not possible */
        gst_gl_display_set_error (display,
            "Cannot upload YUV formats without OpenGL shaders");
      }
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      break;
  }
}


/* called by _init_upload (in the gl thread) */
void
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
    return;
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

  if (!gst_gl_display_check_framebuffer_status (display))
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteTextures (1, &fake_texture);

  _do_upload_make (display, upload);
}

/* Called by the idle function in the gl thread */
void
_do_upload (GstGLDisplay * display, GstGLUpload * upload)
{
  GstVideoFormat v_format;
  guint in_width, in_height, out_width, out_height;

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);
  in_width = upload->in_width;
  in_height = upload->in_height;

  GST_TRACE ("uploading to texture:%u dimensions:%ux%u, "
      "from textures:%u,%u,%u dimensions:%ux%u", upload->out_texture,
      out_width, out_height, upload->in_texture[0], upload->in_texture[1],
      upload->in_texture[2], in_width, in_height);

  _do_upload_fill (display, upload);

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
      if (in_width != out_width || in_height != out_height)
        upload->priv->draw (display, upload);
      /* color space conversion is not needed */
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
    {
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
          /* color space conversion is needed */
#endif
          upload->priv->draw (display, upload);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          /* color space conversion is needed */
          /* not yet supported */
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (in_width != out_width || in_height != out_height)
            upload->priv->draw (display, upload);
          /* color space conversion is not needed */
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }
}

/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
_do_upload_make (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint in_width, in_height;

  gl = display->gl_vtable;

  in_width = upload->in_width;
  in_height = upload->in_height;
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  gl->GenTextures (1, &upload->in_texture[0]);

  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          in_width, in_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
          in_width, in_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          in_width, in_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, NULL);
      break;
    case GST_VIDEO_FORMAT_YUY2:
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
#endif
          gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
              in_width, in_height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
              NULL);
          gl->GenTextures (1, &upload->in_texture[1]);
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              in_width, in_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
              NULL);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, in_width,
              in_height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
      break;
    case GST_VIDEO_FORMAT_UYVY:
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
#endif
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
              in_width, in_height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
              NULL);
          glGenTextures (1, &upload->in_texture[1]);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              in_width, in_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
              NULL);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, in_width,
              in_height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          in_width, in_height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      gl->GenTextures (1, &upload->in_texture[1]);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (in_width) / 2,
          GST_ROUND_UP_2 (in_height) / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          NULL);

      gl->GenTextures (1, &upload->in_texture[2]);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (in_width) / 2,
          GST_ROUND_UP_2 (in_height) / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          NULL);
      break;

    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
_do_upload_fill (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint in_width, in_height, out_width, out_height;

  gl = display->gl_vtable;

  in_width = upload->in_width;
  in_height = upload->in_height;
  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

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
      /* color space conversion is not needed */
      if (in_width != out_width || in_height != out_height)
        gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      else
        gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
#endif
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (in_width != out_width || in_height != out_height)
            gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          else
            gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGB:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGB, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_BGR:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_BGR, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGBA, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_BGRA, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_YUY2:
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
#endif
          gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[0]);

          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
              GST_ROUND_UP_2 (in_width) / 2, in_height,
              GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, upload->data[0]);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA,
              upload->data[0]);
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
      break;
    case GST_VIDEO_FORMAT_UYVY:
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
#endif
          gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[0]);

          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
              GST_ROUND_UP_2 (in_width) / 2, in_height,
              GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, upload->data[0]);
#if 0
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          gl->TexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA,
              upload->data[0]);
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
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
}

#if GST_GL_HAVE_OPENGL
/* called by _do_upload (in the gl thread) */
static void
_do_upload_draw_opengl (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;
  guint in_width = upload->in_width;
  guint in_height = upload->in_height;

  gfloat verts[8] = { 1.0f, -1.0f,
    -1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f
  };
  gint texcoords[8] = { in_width, 0,
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
    {
      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();

      gl->Enable (GL_TEXTURE_RECTANGLE_ARB);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
      gl->TexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    }
      break;

    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    {
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
        {
#endif
          gst_gl_shader_use (upload->shader);

          gl->MatrixMode (GL_PROJECTION);
          gl->LoadIdentity ();

          gl->ActiveTexture (GL_TEXTURE1);
          gst_gl_shader_set_uniform_1i (upload->shader, "UVtex", 1);
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);

          gl->ActiveTexture (GL_TEXTURE0);
          gst_gl_shader_set_uniform_1i (upload->shader, "Ytex", 0);
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);
#if 0
        }
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
        {

          gl->MatrixMode (GL_PROJECTION);
          gl->LoadIdentity ();
          gl->Enable (GL_TEXTURE_RECTANGLE_ARB);
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);
          gl->TexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        }
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
    }
      break;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
      gst_gl_shader_use (upload->shader);

      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();

      gl->ActiveTexture (GL_TEXTURE1);
      gst_gl_shader_set_uniform_1i (upload->shader, "Utex", 1);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      gl->ActiveTexture (GL_TEXTURE2);
      gst_gl_shader_set_uniform_1i (upload->shader, "Vtex", 2);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (upload->shader, "Ytex", 0);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    case GST_VIDEO_FORMAT_AYUV:
    {
      gst_gl_shader_use (upload->shader);

      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (upload->shader, "tex", 0);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }                             /* end switch display->currentVideo_format */

  gl->EnableClientState (GL_VERTEX_ARRAY);
  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->VertexPointer (2, GL_FLOAT, 0, &verts);
  gl->TexCoordPointer (2, GL_INT, 0, &texcoords);

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
}
#endif

#if GST_GL_HAVE_GLES2
static void
_do_upload_draw_gles2 (GstGLDisplay * display, GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;

  GLint viewport_dim[4];

  const GLfloat vVertices[] = { 1.0f, -1.0f, 0.0f,
    1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
    0.0f, .0f,
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
    {
      gl->VertexAttribPointer (upload->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      gl->VertexAttribPointer (upload->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      gl->EnableVertexAttribArray (upload->shader_attr_position_loc);
      gl->EnableVertexAttribArray (upload->shader_attr_texture_loc);

      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    {
#if 0
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
        {
#endif
          gst_gl_shader_use (upload->shader);

          gl->VertexAttribPointer (upload->shader_attr_position_loc, 3,
              GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
          gl->VertexAttribPointer (upload->shader_attr_texture_loc, 2,
              GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

          gl->EnableVertexAttribArray (upload->shader_attr_position_loc);
          gl->EnableVertexAttribArray (upload->shader_attr_texture_loc);

          gl->ActiveTexture (GL_TEXTURE1);
          gst_gl_shader_set_uniform_1i (upload->shader, "UVtex", 1);
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);

          gl->ActiveTexture (GL_TEXTURE0);
          gst_gl_shader_set_uniform_1i (upload->shader, "Ytex", 0);
          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);
#if 0
        }
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
        {

          gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);
        }
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
#endif
    }
      break;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
      gst_gl_shader_use (upload->shader);

      gl->VertexAttribPointer (upload->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      gl->VertexAttribPointer (upload->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      gl->EnableVertexAttribArray (upload->shader_attr_position_loc);
      gl->EnableVertexAttribArray (upload->shader_attr_texture_loc);

      gl->ActiveTexture (GL_TEXTURE1);
      gst_gl_shader_set_uniform_1i (upload->shader, "Utex", 1);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      gl->ActiveTexture (GL_TEXTURE2);
      gst_gl_shader_set_uniform_1i (upload->shader, "Vtex", 2);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (upload->shader, "Ytex", 0);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    case GST_VIDEO_FORMAT_AYUV:
    {
      gst_gl_shader_use (upload->shader);

      gl->VertexAttribPointer (upload->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      gl->VertexAttribPointer (upload->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      gl->EnableVertexAttribArray (upload->shader_attr_position_loc);
      gl->EnableVertexAttribArray (upload->shader_attr_texture_loc);

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (upload->shader, "tex", 0);
      gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;

  }                             /* end switch display->currentVideo_format */

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  /* we are done with the shader */
  gst_gl_display_clear_shader (display);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  gst_gl_display_check_framebuffer_status (display);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
}
#endif
